// Copyright 2023 Ant Group Co., Ltd.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "psi/rr22/rr22_oprf.h"

#include <algorithm>
#include <future>

#include "fmt/format.h"
#include "spdlog/spdlog.h"
#include "yacl/base/buffer.h"
#include "yacl/base/byte_container_view.h"
#include "yacl/crypto/rand/rand.h"
#include "yacl/crypto/tools/prg.h"
#include "yacl/crypto/tools/ro.h"
#include "yacl/math/f2k/f2k.h"
#include "yacl/utils/parallel.h"

#include "psi/rr22/davis_meyer_hash.h"
#include "psi/rr22/okvs/galois128.h"

namespace psi::rr22 {

namespace {

constexpr uint128_t kAesHashSeed =
    yacl::MakeUint128(0x99e096a63468f39f, 0x9ceaad9f20cc8233);

constexpr size_t kPaxosWeight = 3;

}  // namespace

#define USE_MOCK 0

MocRr22VoleSender::MocRr22VoleSender(uint128_t seed) : seed_(seed) {
  uint128_t seed1 = yacl::MakeUint128(0xe0, 0x0f);
  yacl::crypto::Prg<uint128_t> prng1(seed1);

  prng1.Fill(absl::MakeSpan(&delta_, 1));
}

void MocRr22VoleSender::Send(
    [[maybe_unused]] const std::shared_ptr<yacl::link::Context>& lctx,
    absl::Span<uint128_t> c) {
  size_t vole_num = c.size();

  std::vector<uint128_t> a(vole_num);

  std::vector<uint128_t> b(vole_num);

  yacl::crypto::Prg<uint128_t> prng2(seed_);
  prng2.Fill(absl::MakeSpan(a));

  prng2.Fill(absl::MakeSpan(b));

  okvs::Galois128 delta_gf128(delta_);

  for (size_t i = 0; i < vole_num; ++i) {
    c[i] = b[i] ^ (delta_gf128 * a[i]).get<uint128_t>(0);
  }
}

void MocRr22VoleSender::SendF64(
    [[maybe_unused]] const std::shared_ptr<yacl::link::Context>& lctx,
    absl::Span<uint128_t> c) {
  size_t vole_num = c.size();

  std::vector<uint64_t> a(vole_num);

  std::vector<uint128_t> b(vole_num);

  yacl::crypto::Prg<uint64_t> prng(seed_);
  yacl::crypto::Prg<uint128_t> prng2(seed_ + 1);

  prng.Fill(absl::MakeSpan(a));

  prng2.Fill(absl::MakeSpan(b));

  okvs::Galois128 delta_gf128(delta_);

  for (size_t i = 0; i < vole_num; ++i) {
    c[i] = b[i] ^ (delta_gf128 * a[i]).get<uint128_t>(0);
  }
}

MocRr22VoleReceiver::MocRr22VoleReceiver(uint128_t seed) : seed_(seed) {}

void MocRr22VoleReceiver::Recv(
    [[maybe_unused]] const std::shared_ptr<yacl::link::Context>& lctx,
    absl::Span<uint128_t> a, absl::Span<uint128_t> b) {
  yacl::crypto::Prg<uint128_t> prng(seed_);

  SPDLOG_INFO("rank:{}", lctx->Rank());

  prng.Fill(a);
  prng.Fill(b);
}

void MocRr22VoleReceiver::RecvF64(
    [[maybe_unused]] const std::shared_ptr<yacl::link::Context>& lctx,
    absl::Span<uint64_t> a, absl::Span<uint128_t> b) {
  SPDLOG_INFO("rank:{}", lctx->Rank());
  yacl::crypto::Prg<uint64_t> prng(seed_);
  yacl::crypto::Prg<uint128_t> prng2(seed_ + 1);

  prng.Fill(a);
  prng2.Fill(b);
}

void Rr22OprfSender::Send(const std::shared_ptr<yacl::link::Context>& lctx,
                          size_t paxos_init_size,
                          absl::Span<const uint128_t> inputs,
                          absl::Span<uint128_t> hash_outputs,
                          [[maybe_unused]] size_t num_threads) {
  if (mode_ == Rr22PsiMode::FastMode) {
    SendFast(lctx, paxos_init_size, inputs, hash_outputs, num_threads);
  } else if (mode_ == Rr22PsiMode::LowCommMode) {
    SendLowComm(lctx, paxos_init_size, inputs, hash_outputs, num_threads);
  }
}

void Rr22OprfSender::SendFast(const std::shared_ptr<yacl::link::Context>& lctx,
                              size_t paxos_init_size,
                              absl::Span<const uint128_t> inputs,
                              absl::Span<uint128_t> hash_outputs,
                              [[maybe_unused]] size_t num_threads) {
  uint128_t baxos_seed;
  SPDLOG_INFO("recv paxos seed...");

  yacl::Buffer baxos_seed_buf =
      lctx->Recv(lctx->NextRank(), fmt::format("recv paxos seed"));
  YACL_ENFORCE(baxos_seed_buf.size() == sizeof(uint128_t));

  SPDLOG_INFO("recv paxos seed finished");

  std::memcpy(&baxos_seed, baxos_seed_buf.data(), baxos_seed_buf.size());

  baxos_.Init(paxos_init_size, bin_size_, kPaxosWeight, ssp_,
              okvs::PaxosParam::DenseType::GF128, baxos_seed);

  uint128_t ws = 0;
  if (malicious_) {
    SPDLOG_INFO("malicious version");
    // generate ws
    ws = yacl::crypto::SecureRandU128();
    // send hash of ws to receiver
    yacl::crypto::RandomOracle ro(yacl::crypto::HashAlgorithm::SHA256, 32);
    yacl::Buffer ws_hash =
        ro(yacl::ByteContainerView(&ws, sizeof(uint128_t)), 32);

    lctx->SendAsyncThrottled(lctx->NextRank(), ws_hash,
                             fmt::format("send ws_hash"));
  }

  paxos_size_ = baxos_.size();

  // vole send function
  yacl::crypto::SilentVoleSender vole_sender(code_type_, malicious_);

  b_ = yacl::Buffer(std::max<size_t>(256, baxos_.size()) * sizeof(uint128_t));
  absl::Span<uint128_t> b128_span =
      absl::MakeSpan(reinterpret_cast<uint128_t*>(b_.data()),
                     std::max<size_t>(256, baxos_.size()));

  SPDLOG_INFO("begin vole send");

  vole_sender.Send(lctx, b128_span);
  delta_ = vole_sender.GetDelta();

  SPDLOG_INFO("end vole send");

  auto hash_inputs_proc =
      std::async([&] { HashInputMulDelta(inputs, hash_outputs); });

  if (malicious_) {
    yacl::Buffer wr_buf = lctx->Recv(lctx->NextRank(), fmt::format("recv wr"));
    YACL_ENFORCE(wr_buf.size() == sizeof(uint128_t));
    std::memcpy(&w_, wr_buf.data(), wr_buf.size());

    lctx->SendAsyncThrottled(lctx->NextRank(),
                             yacl::ByteContainerView(&ws, sizeof(uint128_t)),
                             fmt::format("recv ws"));

    w_ = w_ ^ ws;
  }

  SPDLOG_INFO("recv paxos solve ...");

  yacl::Buffer paxos_solve_buffer(paxos_size_ * sizeof(uint128_t));
  absl::Span<uint128_t> paxos_solve_vec =
      absl::MakeSpan((uint128_t*)paxos_solve_buffer.data(), paxos_size_);

  size_t recv_item_count = 0;

  while (true) {
    yacl::Buffer paxos_solve_buf =
        lctx->Recv(lctx->NextRank(), fmt::format("recv paxos_solve"));

    std::memcpy(&paxos_solve_vec[recv_item_count], paxos_solve_buf.data(),
                paxos_solve_buf.size());

    recv_item_count += paxos_solve_buf.size() / sizeof(uint128_t);
    if (recv_item_count == paxos_size_) {
      break;
    }
  }

  SPDLOG_INFO("recv paxos solve finished. bytes:{}",
              paxos_solve_vec.size() * sizeof(uint128_t));

  hash_inputs_proc.get();

  okvs::Galois128 delta_gf128(delta_);

  SPDLOG_INFO("begin b xor delta a");

  yacl::parallel_for(
      0, paxos_solve_vec.size(), [&](int64_t begin, int64_t end) {
        for (int64_t idx = begin; idx < end; ++idx) {
          b128_span[idx] =
              b128_span[idx] ^
              (delta_gf128 * paxos_solve_vec[idx]).get<uint128_t>(0);
        }
      });

  SPDLOG_INFO("end b xor delta a");
}

void Rr22OprfSender::SendLowComm(
    const std::shared_ptr<yacl::link::Context>& lctx, size_t paxos_init_size,
    absl::Span<const uint128_t> inputs, absl::Span<uint128_t> hash_outputs,
    [[maybe_unused]] size_t num_threads) {
  uint128_t paxos_seed;
  SPDLOG_INFO("recv paxos seed...");

  yacl::Buffer paxos_seed_buf =
      lctx->Recv(lctx->NextRank(), fmt::format("recv paxos seed"));
  YACL_ENFORCE(paxos_seed_buf.size() == sizeof(uint128_t));

  std::memcpy(&paxos_seed, paxos_seed_buf.data(), paxos_seed_buf.size());

  paxos_.Init(paxos_init_size, kPaxosWeight, ssp_,
              okvs::PaxosParam::DenseType::GF128, paxos_seed);

  paxos_size_ = paxos_.size();

  // vole send function

  yacl::crypto::SilentVoleSender vole_sender(code_type_);

  b_ = yacl::Buffer(std::max<size_t>(256, paxos_.size()) * sizeof(uint128_t));
  absl::Span<uint128_t> b128_span =
      absl::MakeSpan(reinterpret_cast<uint128_t*>(b_.data()),
                     std::max<size_t>(256, paxos_.size()));

  SPDLOG_INFO("begin vole send");

  vole_sender.SfSend(lctx, b128_span);
  delta_ = vole_sender.GetDelta();

  SPDLOG_INFO("end vole send");

  HashInputMulDelta(inputs, hash_outputs);

  SPDLOG_INFO("recv paxos solve ...");
  yacl::Buffer paxos_solve_buf =
      lctx->Recv(lctx->NextRank(), fmt::format("recv paxos_solve"));
  YACL_ENFORCE(paxos_solve_buf.size() / sizeof(uint64_t) == paxos_size_);

  SPDLOG_INFO("recv paxos solve finished. bytes:{}", paxos_solve_buf.size());

  absl::Span<uint64_t> paxos_solve_u64 =
      absl::MakeSpan(reinterpret_cast<uint64_t*>(paxos_solve_buf.data()),
                     paxos_solve_buf.size() / sizeof(uint64_t));

  SPDLOG_INFO("paxos_solve_u64 size:{}", paxos_solve_u64.size());

  okvs::Galois128 delta_gf128(delta_);

  for (size_t i = 0; i < paxos_solve_u64.size(); ++i) {
    b128_span[i] =
        b128_span[i] ^ (delta_gf128 * paxos_solve_u64[i]).get<uint128_t>(0);
  }
}

void Rr22OprfSender::Eval(absl::Span<const uint128_t> inputs,
                          absl::Span<uint128_t> outputs, uint64_t num_threads) {
  SPDLOG_INFO("paxos decode ...");

  YACL_ENFORCE(b_.size() > 0, "Must use Send() first");

  absl::Span<uint128_t> b128_span =
      absl::MakeSpan(reinterpret_cast<uint128_t*>(b_.data()), paxos_size_);

  if (mode_ == Rr22PsiMode::FastMode) {
    baxos_.Decode(inputs, outputs, b128_span, num_threads);
  } else if (mode_ == Rr22PsiMode::LowCommMode) {
    paxos_.Decode(inputs, outputs, b128_span);
  } else {
    YACL_THROW("unsupported rr22 psi mode");
  }

  SPDLOG_INFO("paxos decode finished");

  okvs::AesCrHash aes_crhash(kAesHashSeed);

  okvs::Galois128 delta_gf128(delta_);

  if (mode_ == Rr22PsiMode::FastMode) {
    yacl::parallel_for(0, inputs.size(), [&](int64_t begin, int64_t end) {
      for (int64_t idx = begin; idx < end; ++idx) {
        uint128_t h = aes_crhash.Hash(inputs[idx]);
        outputs[idx] = outputs[idx] ^ (delta_gf128 * h).get<uint128_t>(0);
        if (malicious_) {
          outputs[idx] = outputs[idx] ^ w_;
        }
      }
    });
  } else if (mode_ == Rr22PsiMode::LowCommMode) {
    yacl::parallel_for(0, inputs.size(), [&](int64_t begin, int64_t end) {
      for (int64_t idx = begin; idx < end; ++idx) {
        uint64_t h =
            yacl::DecomposeUInt128(aes_crhash.Hash(inputs[idx])).second;
        outputs[idx] = outputs[idx] ^ (delta_gf128 * h).get<uint128_t>(0);
      }
    });
  }
  if (malicious_) {
    DavisMeyerHash(outputs, inputs, outputs);
  } else {
    aes_crhash.Hash(outputs, outputs);
  }
}

void Rr22OprfSender::HashInputMulDelta(absl::Span<const uint128_t> inputs,
                                       absl::Span<uint128_t> hash_outputs) {
  YACL_ENFORCE(hash_outputs.size() == inputs.size());

  okvs::Galois128 delta_gf128(delta_);
  okvs::AesCrHash aes_crhash(kAesHashSeed);

  if (mode_ == Rr22PsiMode::FastMode) {
    for (size_t i = 0; i < inputs.size(); ++i) {
      hash_outputs[i] =
          (delta_gf128 * aes_crhash.Hash(inputs[i])).get<uint128_t>(0);
    }
  } else if (mode_ == Rr22PsiMode::LowCommMode) {
    for (size_t i = 0; i < inputs.size(); ++i) {
      hash_outputs[i] =
          (delta_gf128 *
           yacl::DecomposeUInt128(aes_crhash.Hash(inputs[i])).second)
              .get<uint128_t>(0);
    }
  } else {
    YACL_THROW("unsupported rr22 psi mode");
  }
}

void Rr22OprfSender::Eval(absl::Span<const uint128_t> inputs,
                          absl::Span<const uint128_t> inputs_hash,
                          absl::Span<uint128_t> outputs, uint64_t num_threads) {
  YACL_ENFORCE(b_.size() > 0, "Must use Send() first");

  absl::Span<uint128_t> b128_span =
      absl::MakeSpan(reinterpret_cast<uint128_t*>(b_.data()), paxos_size_);

  SPDLOG_INFO("paxos decode (mode:{}) ...",
              mode_ == Rr22PsiMode::FastMode ? "Fast" : "LowComm");

  if (mode_ == Rr22PsiMode::FastMode) {
    baxos_.Decode(inputs, outputs, b128_span, num_threads);

  } else if (mode_ == Rr22PsiMode::LowCommMode) {
    paxos_.Decode(inputs, outputs, b128_span);
  } else {
    YACL_THROW("unsupported rr22 psi mode");
  }

  SPDLOG_INFO("paxos decode finished");

  yacl::parallel_for(0, inputs.size(), [&](int64_t begin, int64_t end) {
    for (int64_t idx = begin; idx < end; ++idx) {
      outputs[idx] = outputs[idx] ^ inputs_hash[idx];

      if (malicious_) {
        outputs[idx] = outputs[idx] ^ w_;
      }
    }
  });

  if (malicious_) {
    DavisMeyerHash(outputs, inputs, outputs);
  } else {
    okvs::AesCrHash aes_crhash(kAesHashSeed);

    aes_crhash.Hash(outputs, outputs);
  }
}

void Rr22OprfReceiver::Recv(const std::shared_ptr<yacl::link::Context>& lctx,
                            size_t paxos_init_size,
                            const std::vector<uint128_t>& inputs,
                            absl::Span<uint128_t> outputs, size_t num_threads) {
  if (mode_ == Rr22PsiMode::FastMode) {
    RecvFast(lctx, paxos_init_size, inputs, outputs, num_threads);
  } else if (mode_ == Rr22PsiMode::LowCommMode) {
    RecvLowComm(lctx, paxos_init_size, inputs, outputs, num_threads);
  }
}

void Rr22OprfReceiver::RecvFast(
    const std::shared_ptr<yacl::link::Context>& lctx, size_t paxos_init_size,
    const std::vector<uint128_t>& inputs, absl::Span<uint128_t> outputs,
    size_t num_threads) {
  YACL_ENFORCE(inputs.size() <= paxos_init_size);

  okvs::Baxos paxos;

  uint128_t paxos_seed = yacl::crypto::SecureRandU128();

  yacl::ByteContainerView paxos_seed_buf(&paxos_seed, sizeof(uint128_t));

  lctx->SendAsyncThrottled(lctx->NextRank(), paxos_seed_buf,
                           fmt::format("send paxos_seed_buf"));
  paxos.Init(paxos_init_size, bin_size_, kPaxosWeight, ssp_,
             okvs::PaxosParam::DenseType::GF128, paxos_seed);

  uint128_t w = 0;
  uint128_t wr = 0;
  yacl::Buffer ws_hash_buf;
  if (malicious_) {
    SPDLOG_INFO("malicious version");
    // generate wr
    wr = yacl::crypto::SecureRandU128();
    ws_hash_buf = lctx->Recv(lctx->NextRank(), fmt::format("recv ws_hash"));
    YACL_ENFORCE(ws_hash_buf.size() == 32);
  }

  paxos_size_ = paxos.size();

  // c + b = a * delta
  yacl::Buffer a;
  yacl::Buffer c;
  absl::Span<uint128_t> a128_span;
  absl::Span<uint128_t> c128_span;

  // vole recv function
  auto vole_recv_proc = std::async([&] {
    yacl::crypto::SilentVoleReceiver vole_receiver(code_type_, malicious_);

    a = yacl::Buffer(std::max<size_t>(256, paxos.size()) * sizeof(uint128_t));
    c = yacl::Buffer(std::max<size_t>(256, paxos.size()) * sizeof(uint128_t));

    a128_span = absl::MakeSpan(reinterpret_cast<uint128_t*>(a.data()),
                               std::max<size_t>(256, paxos.size()));
    c128_span = absl::MakeSpan(reinterpret_cast<uint128_t*>(c.data()),
                               std::max<size_t>(256, paxos.size()));

    SPDLOG_INFO("a_,b_ size:{} {}", a128_span.size(), c128_span.size());

    SPDLOG_INFO("begin vole recv");

    vole_receiver.Recv(lctx, a128_span, c128_span);

    SPDLOG_INFO("end vole recv");
  });

  okvs::AesCrHash aes_crhash(kAesHashSeed);

  aes_crhash.Hash(absl::MakeSpan(inputs), outputs);

  yacl::Buffer p_buffer(paxos.size() * sizeof(uint128_t));
  std::memset((uint8_t*)p_buffer.data(), 0, p_buffer.size());

  absl::Span<uint128_t> p128_span =
      absl::MakeSpan((uint128_t*)(p_buffer.data()), paxos.size());

  SPDLOG_INFO("solve begin");
  paxos.Solve(absl::MakeSpan(inputs), outputs, p128_span, nullptr, num_threads);
  SPDLOG_INFO("solve end");

  vole_recv_proc.get();

  if (malicious_) {
    // send wr
    lctx->SendAsyncThrottled(lctx->NextRank(),
                             yacl::ByteContainerView(&wr, sizeof(uint128_t)),
                             fmt::format("send wr"));

    // recv sender's ws
    yacl::Buffer ws_buf = lctx->Recv(lctx->NextRank(), fmt::format("recv ws"));
    YACL_ENFORCE(ws_buf.size() == sizeof(uint128_t));

    yacl::crypto::RandomOracle ro(yacl::crypto::HashAlgorithm::SHA256, 32);
    yacl::Buffer ws_hash = ro(ws_buf, 32);
    // check ws and hash of ws
    // if check failed, aborts protocol
    YACL_ENFORCE(
        std::memcmp(ws_hash.data(), ws_hash_buf.data(), ws_hash.size()) == 0,
        "server seed not match");
    uint128_t ws;
    std::memcpy(&ws, ws_buf.data(), sizeof(uint128_t));
    w = wr ^ ws;
  }

  auto oprf_eval_proc = std::async([&] {
    SPDLOG_INFO("begin compute self oprf");
    paxos.Decode(absl::MakeSpan(inputs), outputs,
                 absl::MakeSpan(c128_span.data(), paxos.size()), num_threads);

    if (malicious_) {
      for (size_t i = 0; i < outputs.size(); ++i) {
        outputs[i] = outputs[i] ^ w;
      }
    }

    if (malicious_) {
      SPDLOG_INFO("call Davis-Meyer hash");
      DavisMeyerHash(outputs, inputs, outputs);
    } else {
      SPDLOG_INFO("call aes crhash");
      aes_crhash.Hash(outputs, outputs);
    }
    SPDLOG_INFO("end compute self oprf");
  });

  SPDLOG_INFO("begin p xor a");

  yacl::parallel_for(0, p128_span.size(), [&](int64_t begin, int64_t end) {
    for (int64_t idx = begin; idx < end; ++idx) {
      p128_span[idx] = p128_span[idx] ^ a128_span[idx];
    }
  });

  SPDLOG_INFO("end p xor a");

  for (size_t i = 0; i < p128_span.size(); i += 100000) {
    size_t batch_size = std::min<size_t>(100000, p128_span.size() - i);
    yacl::ByteContainerView paxos_solve_byteview(
        &p128_span[i], batch_size * sizeof(uint128_t));

    lctx->SendAsyncThrottled(lctx->NextRank(), paxos_solve_byteview,
                             fmt::format("send paxos_solve_byteview"));
  }

  oprf_eval_proc.get();
}

void Rr22OprfReceiver::RecvLowComm(
    const std::shared_ptr<yacl::link::Context>& lctx, size_t paxos_init_size,
    const std::vector<uint128_t>& inputs, absl::Span<uint128_t> outputs,
    [[maybe_unused]] size_t num_threads) {
  YACL_ENFORCE(inputs.size() <= paxos_init_size);

  okvs::Paxos<uint32_t> paxos;

  uint128_t paxos_seed = yacl::crypto::SecureRandU128();

  yacl::ByteContainerView paxos_seed_buf(&paxos_seed, sizeof(uint128_t));

  lctx->SendAsyncThrottled(lctx->NextRank(), paxos_seed_buf,
                           fmt::format("send paxos_seed_buf"));

  paxos.Init(paxos_init_size, kPaxosWeight, ssp_,
             okvs::PaxosParam::DenseType::GF128, paxos_seed);

  paxos_size_ = paxos.size();

  // c + b = a * delta
  yacl::Buffer a;
  yacl::Buffer c;
  absl::Span<uint64_t> a64_span;
  absl::Span<uint128_t> c128_span;

  // vole recv function
  auto vole_recv_proc = std::async([&] {
    SPDLOG_INFO("use SilentVoleReceiver");
    yacl::crypto::SilentVoleReceiver vole_receiver(code_type_);

    a = yacl::Buffer(std::max<size_t>(256, paxos.size()) * sizeof(uint64_t));
    c = yacl::Buffer(std::max<size_t>(256, paxos.size()) * sizeof(uint128_t));

    a64_span = absl::MakeSpan(reinterpret_cast<uint64_t*>(a.data()),
                              std::max<size_t>(256, paxos.size()));
    c128_span = absl::MakeSpan(reinterpret_cast<uint128_t*>(c.data()),
                               std::max<size_t>(256, paxos.size()));
    SPDLOG_INFO("a_,b_ size:{} {} ", a64_span.size(), c128_span.size());

    SPDLOG_INFO("begin vole recv");

    vole_receiver.SfRecv(lctx, absl::MakeSpan(a64_span), c128_span);

    SPDLOG_INFO("end vole recv");
  });

  okvs::AesCrHash aes_crhash(kAesHashSeed);

  aes_crhash.Hash(absl::MakeSpan(inputs), outputs);

  yacl::Buffer p_buffer(paxos.size() * sizeof(uint128_t));
  std::memset((uint8_t*)p_buffer.data(), 0, p_buffer.size());

  absl::Span<uint128_t> p128_span((uint128_t*)p_buffer.data(), paxos.size());
  absl::Span<uint64_t> p64_span((uint64_t*)p_buffer.data(), paxos.size());

  SPDLOG_INFO("solve begin");
  paxos.SetInput(absl::MakeSpan(inputs));

  paxos.Encode(outputs, p128_span, nullptr);

  SPDLOG_INFO("solve end");

  vole_recv_proc.get();

  auto oprf_eval_proc = std::async([&] {
    SPDLOG_INFO("begin receiver oprf");
    paxos.Decode(absl::MakeSpan(inputs), outputs,
                 absl::MakeSpan(c128_span.data(), paxos.size()));

    aes_crhash.Hash(outputs, outputs);
    SPDLOG_INFO("end receiver oprf");
  });

  SPDLOG_INFO("begin p xor a");

  for (size_t i = 0; i < p64_span.size(); ++i) {
    p64_span[i] = a64_span[i] ^ yacl::DecomposeUInt128(p128_span[i]).second;
  }

  SPDLOG_INFO("end p xor a");

  yacl::ByteContainerView paxos_solve_byteview(
      p64_span.data(), p64_span.size() * sizeof(uint64_t));

  lctx->SendAsyncThrottled(lctx->NextRank(), paxos_solve_byteview,
                           fmt::format("send paxos_solve_byteview"));

  oprf_eval_proc.get();
}

}  // namespace psi::rr22
