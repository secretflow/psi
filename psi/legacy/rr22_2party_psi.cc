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

#include "psi/legacy/rr22_2party_psi.h"

#include <chrono>
#include <future>

#include "omp.h"
#include "sparsehash/dense_hash_map"
#include "yacl/crypto/hash/hash_utils.h"
#include "yacl/utils/parallel.h"

#include "psi/legacy/factory.h"
#include "psi/rr22/davis_meyer_hash.h"
#include "psi/rr22/okvs/galois128.h"
#include "psi/utils/sync.h"

using DurationMillis = std::chrono::duration<double, std::milli>;

namespace psi {

namespace {

struct NoHash {
  inline size_t operator()(const uint128_t& v) const {
    uint32_t v32;
    std::memcpy(&v32, &v, sizeof(uint32_t));

    return v32;
  }
};

size_t ComputeTruncateSize(size_t self_size, size_t peer_size, size_t ssp,
                           bool malicious) {
  size_t truncate_size =
      malicious
          ? sizeof(uint128_t)
          : std::min<size_t>(
                std::ceil((ssp + std::ceil(std::log2(self_size * peer_size))) /
                          8),
                sizeof(uint128_t));

  return truncate_size;
}

constexpr size_t kRr22OprfBinSize = 1 << 14;
constexpr uint128_t kAesHashSeed =
    yacl::MakeUint128(0x99e096a63468f39f, 0x9ceaad9f20cc8233);

constexpr size_t kPaxosWeight = 3;

}  // namespace

void Rr22OprfSender::Send(const std::shared_ptr<yacl::link::Context>& lctx,
                          size_t paxos_init_size,
                          absl::Span<const uint128_t> inputs,
                          absl::Span<uint128_t> hash_outputs,
                          [[maybe_unused]] size_t num_threads) {
  if (mode_ == rr22::Rr22PsiMode::FastMode) {
    SendFast(lctx, paxos_init_size, inputs, hash_outputs, num_threads);
  } else if (mode_ == rr22::Rr22PsiMode::LowCommMode) {
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
              rr22::okvs::PaxosParam::DenseType::GF128, baxos_seed);

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
  std::memset(b_.data(), 0, b_.size());
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
  std::memset(paxos_solve_buffer.data(), 0, paxos_solve_buffer.size());
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

  rr22::okvs::Galois128 delta_gf128(delta_);

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
              rr22::okvs::PaxosParam::DenseType::Binary, paxos_seed);

  paxos_size_ = paxos_.size();

  // vole send function

  yacl::crypto::SilentVoleSender vole_sender(code_type_);

  b_ = yacl::Buffer(std::max<size_t>(256, paxos_.size()) * sizeof(uint128_t));
  std::memset(b_.data(), 0, b_.size());
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

  rr22::okvs::Galois128 delta_gf128(delta_);

  for (size_t i = 0; i < paxos_solve_u64.size(); ++i) {
    // Delta * (A - P), note that here is GF64 * GF128 = GF128
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

  if (mode_ == rr22::Rr22PsiMode::FastMode) {
    baxos_.Decode(inputs, outputs, b128_span, num_threads);
  } else if (mode_ == rr22::Rr22PsiMode::LowCommMode) {
    paxos_.Decode(inputs, outputs, b128_span);
  } else {
    YACL_THROW("unsupported rr22 psi mode");
  }

  SPDLOG_INFO("paxos decode finished");

  rr22::okvs::AesCrHash aes_crhash(kAesHashSeed);

  rr22::okvs::Galois128 delta_gf128(delta_);

  if (mode_ == rr22::Rr22PsiMode::FastMode) {
    yacl::parallel_for(0, inputs.size(), [&](int64_t begin, int64_t end) {
      for (int64_t idx = begin; idx < end; ++idx) {
        uint128_t h = aes_crhash.Hash(inputs[idx]);
        outputs[idx] = outputs[idx] ^ (delta_gf128 * h).get<uint128_t>(0);
        if (malicious_) {
          outputs[idx] = outputs[idx] ^ w_;
        }
      }
    });
  } else if (mode_ == rr22::Rr22PsiMode::LowCommMode) {
    yacl::parallel_for(0, inputs.size(), [&](int64_t begin, int64_t end) {
      for (int64_t idx = begin; idx < end; ++idx) {
        uint64_t h =
            yacl::DecomposeUInt128(aes_crhash.Hash(inputs[idx])).second;
        // delta_gf128 * h is GF128 * GF64
        outputs[idx] = outputs[idx] ^ (delta_gf128 * h).get<uint128_t>(0);
      }
    });
  }
  if (malicious_) {
    rr22::DavisMeyerHash(outputs, inputs, outputs);
  } else {
    aes_crhash.Hash(outputs, outputs);
  }
}

void Rr22OprfSender::HashInputMulDelta(absl::Span<const uint128_t> inputs,
                                       absl::Span<uint128_t> hash_outputs) {
  YACL_ENFORCE(hash_outputs.size() == inputs.size());

  rr22::okvs::Galois128 delta_gf128(delta_);
  rr22::okvs::AesCrHash aes_crhash(kAesHashSeed);

  if (mode_ == rr22::Rr22PsiMode::FastMode) {
    for (size_t i = 0; i < inputs.size(); ++i) {
      hash_outputs[i] =
          (delta_gf128 * aes_crhash.Hash(inputs[i])).get<uint128_t>(0);
    }
  } else if (mode_ == rr22::Rr22PsiMode::LowCommMode) {
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
              mode_ == rr22::Rr22PsiMode::FastMode ? "Fast" : "LowComm");

  if (mode_ == rr22::Rr22PsiMode::FastMode) {
    baxos_.Decode(inputs, outputs, b128_span, num_threads);

  } else if (mode_ == rr22::Rr22PsiMode::LowCommMode) {
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
    rr22::DavisMeyerHash(outputs, inputs, outputs);
  } else {
    rr22::okvs::AesCrHash aes_crhash(kAesHashSeed);

    aes_crhash.Hash(outputs, outputs);
  }
}

void Rr22OprfReceiver::Recv(const std::shared_ptr<yacl::link::Context>& lctx,
                            size_t paxos_init_size,
                            const std::vector<uint128_t>& inputs,
                            absl::Span<uint128_t> outputs, size_t num_threads) {
  if (mode_ == rr22::Rr22PsiMode::FastMode) {
    RecvFast(lctx, paxos_init_size, inputs, outputs, num_threads);
  } else if (mode_ == rr22::Rr22PsiMode::LowCommMode) {
    RecvLowComm(lctx, paxos_init_size, inputs, outputs, num_threads);
  }
}

void Rr22OprfReceiver::RecvFast(
    const std::shared_ptr<yacl::link::Context>& lctx, size_t paxos_init_size,
    const std::vector<uint128_t>& inputs, absl::Span<uint128_t> outputs,
    size_t num_threads) {
  YACL_ENFORCE(inputs.size() <= paxos_init_size);

  rr22::okvs::Baxos paxos;

  uint128_t paxos_seed = yacl::crypto::SecureRandU128();

  yacl::ByteContainerView paxos_seed_buf(&paxos_seed, sizeof(uint128_t));

  lctx->SendAsyncThrottled(lctx->NextRank(), paxos_seed_buf,
                           fmt::format("send paxos_seed_buf"));
  paxos.Init(paxos_init_size, bin_size_, kPaxosWeight, ssp_,
             rr22::okvs::PaxosParam::DenseType::GF128, paxos_seed);

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
    std::memset(a.data(), 0, a.size());
    std::memset(c.data(), 0, c.size());

    a128_span = absl::MakeSpan(reinterpret_cast<uint128_t*>(a.data()),
                               std::max<size_t>(256, paxos.size()));
    c128_span = absl::MakeSpan(reinterpret_cast<uint128_t*>(c.data()),
                               std::max<size_t>(256, paxos.size()));

    SPDLOG_INFO("a_,b_ size:{} {}", a128_span.size(), c128_span.size());

    SPDLOG_INFO("begin vole recv");

    vole_receiver.Recv(lctx, a128_span, c128_span);

    SPDLOG_INFO("end vole recv");
  });

  rr22::okvs::AesCrHash aes_crhash(kAesHashSeed);

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
      rr22::DavisMeyerHash(outputs, inputs, outputs);
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

    lctx->Send(lctx->NextRank(), paxos_solve_byteview,
               fmt::format("send paxos_solve_byteview"));
  }

  oprf_eval_proc.get();
}

void Rr22OprfReceiver::RecvLowComm(
    const std::shared_ptr<yacl::link::Context>& lctx, size_t paxos_init_size,
    const std::vector<uint128_t>& inputs, absl::Span<uint128_t> outputs,
    [[maybe_unused]] size_t num_threads) {
  YACL_ENFORCE(inputs.size() <= paxos_init_size);

  rr22::okvs::Paxos<uint32_t> paxos;

  uint128_t paxos_seed = yacl::crypto::SecureRandU128();

  yacl::ByteContainerView paxos_seed_buf(&paxos_seed, sizeof(uint128_t));

  lctx->SendAsyncThrottled(lctx->NextRank(), paxos_seed_buf,
                           fmt::format("send paxos_seed_buf"));
  // here we must use DenseType::Binary for supporting EncodeU64, which
  // should used in LowComm
  paxos.Init(paxos_init_size, kPaxosWeight, ssp_,
             rr22::okvs::PaxosParam::DenseType::Binary, paxos_seed);

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
    memset(a.data(), 0, a.size());
    c = yacl::Buffer(std::max<size_t>(256, paxos.size()) * sizeof(uint128_t));
    memset(c.data(), 0, c.size());

    a64_span = absl::MakeSpan(reinterpret_cast<uint64_t*>(a.data()),
                              std::max<size_t>(256, paxos.size()));
    c128_span = absl::MakeSpan(reinterpret_cast<uint128_t*>(c.data()),
                               std::max<size_t>(256, paxos.size()));
    SPDLOG_INFO("a_,b_ size:{} {} ", a64_span.size(), c128_span.size());

    SPDLOG_INFO("begin vole recv");

    vole_receiver.SfRecv(lctx, absl::MakeSpan(a64_span), c128_span);

    SPDLOG_INFO("end vole recv");
  });

  rr22::okvs::AesCrHash aes_crhash(kAesHashSeed);

  for (size_t i = 0; i < inputs.size(); ++i) {
    outputs[i] = aes_crhash.Hash(inputs[i]);
  }

  // in LowComm version, we need to Hash the inputs to subfield, so we only need
  // the low 64 bits
  std::vector<uint64_t> outputs_u64(outputs.size(), 0);
  for (size_t i = 0; i < outputs.size(); ++i) {
    outputs_u64[i] = yacl::DecomposeUInt128(outputs[i]).second;
  }
  absl::Span<uint64_t> outputs_u64_span(outputs_u64);

  // yacl::Buffer p_buffer(paxos.size() * sizeof(uint128_t));
  // yacl::Buffer p_buffer(paxos.size() * sizeof(uint64_t));
  // std::memset(p_buffer.data(), 0, p_buffer.size());

  yacl::Buffer p64_buffer(paxos.size() * sizeof(uint64_t));
  // yacl::Buffer p_buffer(paxos.size() * sizeof(uint64_t));
  std::memset(p64_buffer.data(), 0, p64_buffer.size());

  absl::Span<uint64_t> p64_span((uint64_t*)p64_buffer.data(), paxos.size());
  // absl::Span<uint128_t> p128_span((uint128_t*)p_buffer.data(), paxos.size());

  SPDLOG_INFO("solve begin");
  paxos.SetInput(absl::MakeSpan(inputs));
  SPDLOG_INFO("finished SetInput");

  paxos.EncodeU64(outputs_u64_span, p64_span, nullptr);

  SPDLOG_INFO("solve end");

  vole_recv_proc.get();

  auto oprf_eval_proc = std::async([&] {
    SPDLOG_INFO("begin receiver oprf");
    paxos.Decode(absl::MakeSpan(inputs), outputs,
                 absl::MakeSpan(c128_span.data(), paxos.size()));
    // oprf end output
    aes_crhash.Hash(outputs, outputs);
    SPDLOG_INFO("end receiver oprf");
  });

  SPDLOG_INFO("begin p xor a");

  for (size_t i = 0; i < p64_span.size(); ++i) {
    p64_span[i] ^= a64_span[i];
  }

  SPDLOG_INFO("end p xor a");

  yacl::ByteContainerView paxos_solve_byteview(
      p64_span.data(), p64_span.size() * sizeof(uint64_t));

  lctx->SendAsyncThrottled(lctx->NextRank(), paxos_solve_byteview,
                           fmt::format("send paxos_solve_byteview"));

  oprf_eval_proc.get();
}

std::vector<size_t> GetIntersection(
    absl::Span<const uint128_t> self_oprfs, size_t peer_items_num,
    const std::shared_ptr<yacl::link::Context>& lctx, size_t num_threads,
    bool compress, size_t mask_size) {
  if (!compress) {
    mask_size = sizeof(uint128_t);
  }
  google::dense_hash_map<uint128_t, size_t, NoHash> dense_map(
      self_oprfs.size());
  dense_map.set_empty_key(yacl::MakeUint128(0, 0));
  auto map_f = std::async([&]() {
    auto truncate_mask = yacl::MakeUint128(0, 0);
    if (compress) {
      for (size_t i = 0; i < mask_size; ++i) {
        truncate_mask = 0xff | (truncate_mask << 8);
        SPDLOG_DEBUG(
            "{}, truncate_mask:{}", i,
            (std::ostringstream() << rr22::okvs::Galois128(truncate_mask))
                .str());
      }
    }
    for (size_t i = 0; i < self_oprfs.size(); ++i) {
      if (compress) {
        dense_map.insert(std::make_pair(self_oprfs[i] & truncate_mask, i));
      } else {
        dense_map.insert(std::make_pair(self_oprfs[i], i));
      }
    }
  });
  SPDLOG_INFO("recv rr22 oprf begin");
  auto peer_buffer =
      lctx->Recv(lctx->NextRank(), fmt::format("recv paxos_solve"));
  YACL_ENFORCE(peer_items_num == peer_buffer.size() / mask_size);
  SPDLOG_INFO("recv rr22 oprf finished: {} vector:{}", peer_buffer.size(),
              peer_items_num);
  map_f.get();
  auto* peer_data_ptr = peer_buffer.data<uint8_t>();
  std::mutex merge_mtx;
  std::vector<size_t> indices;
  size_t grain_size = (peer_items_num + num_threads - 1) / num_threads;
  yacl::parallel_for(
      0, peer_items_num, grain_size, [&](int64_t begin, int64_t end) {
        std::vector<uint32_t> tmp_indexs;
        uint128_t data = yacl::MakeUint128(0, 0);
        for (int64_t j = begin; j < end; j++) {
          std::memcpy(&data, peer_data_ptr + (j * mask_size), mask_size);
          auto iter = dense_map.find(data);
          if (iter != dense_map.end()) {
            tmp_indexs.push_back(iter->second);
          }
        }
        if (!tmp_indexs.empty()) {
          std::lock_guard<std::mutex> lock(merge_mtx);
          indices.insert(indices.end(), tmp_indexs.begin(), tmp_indexs.end());
        }
      });
  return indices;
}

void Rr22PsiSenderInternal(const rr22::Rr22PsiOptions& options,
                           const std::shared_ptr<yacl::link::Context>& lctx,
                           const std::vector<uint128_t>& inputs) {
  YACL_ENFORCE(lctx->WorldSize() == 2);

  // Gather Items Size
  std::vector<size_t> items_size = AllGatherItemsSize(lctx, inputs.size());

  size_t sender_size = items_size[lctx->Rank()];
  size_t receiver_size = items_size[lctx->NextRank()];

  YACL_ENFORCE(sender_size == inputs.size());

  if ((sender_size == 0) || (receiver_size == 0)) {
    return;
  }
  YACL_ENFORCE(sender_size <= receiver_size);

  size_t mask_size = sizeof(uint128_t);
  if (options.compress) {
    mask_size = ComputeTruncateSize(sender_size, receiver_size, options.ssp,
                                    options.malicious);
  }

  Rr22OprfSender oprf_sender(kRr22OprfBinSize, options.ssp, options.mode,
                             options.code_type, options.malicious);

  yacl::Buffer inputs_hash_buffer(inputs.size() * sizeof(uint128_t));
  absl::Span<uint128_t> inputs_hash =
      absl::MakeSpan((uint128_t*)inputs_hash_buffer.data(), inputs.size());

  oprf_sender.Send(lctx, receiver_size, absl::MakeSpan(inputs), inputs_hash,
                   options.num_threads);

  yacl::Buffer sender_oprf_buffer(inputs.size() * sizeof(uint128_t));
  absl::Span<uint128_t> sender_oprfs =
      absl::MakeSpan((uint128_t*)(sender_oprf_buffer.data()), inputs.size());

  SPDLOG_INFO("oprf eval begin");
  oprf_sender.Eval(inputs, inputs_hash, sender_oprfs, options.num_threads);

  SPDLOG_INFO("oprf eval end");

  // random shuffle sender's oprf values
  SPDLOG_INFO("oprf shuffle begin");
  std::mt19937 g(yacl::crypto::SecureRandU64());
  std::shuffle(sender_oprfs.begin(), sender_oprfs.end(), g);
  SPDLOG_INFO("oprf shuffle end");

  yacl::ByteContainerView oprf_byteview;
  if (options.compress) {
    uint128_t* src = sender_oprfs.data();
    uint8_t* dest = (uint8_t*)sender_oprfs.data();

    for (size_t i = 0; i < sender_oprfs.size(); ++i) {
      std::memmove(dest, src, mask_size);
      dest += mask_size;
      src += 1;
    }

    oprf_byteview = yacl::ByteContainerView(sender_oprfs.data(),
                                            sender_oprfs.size() * mask_size);
  } else {
    oprf_byteview = yacl::ByteContainerView(
        sender_oprfs.data(), sender_oprfs.size() * sizeof(uint128_t));
  }

  SPDLOG_INFO("send rr22 oprf: {} vector:{}", oprf_byteview.size(),
              sender_oprfs.size());

  yacl::ByteContainerView send_byteview = yacl::ByteContainerView(
      oprf_byteview.data(), sender_oprfs.size() * mask_size);
  // TODO: split send_byteview then send or may cause recv timeout
  lctx->SendAsyncThrottled(lctx->NextRank(), send_byteview,
                           fmt::format("send oprf_buf"));

  SPDLOG_INFO("send rr22 oprf finished");
}

std::vector<size_t> Rr22PsiReceiverInternal(
    const rr22::Rr22PsiOptions& options,
    const std::shared_ptr<yacl::link::Context>& lctx,
    const std::vector<uint128_t>& inputs) {
  YACL_ENFORCE(lctx->WorldSize() == 2);

  // Gather Items Size
  std::vector<size_t> items_size = AllGatherItemsSize(lctx, inputs.size());

  size_t receiver_size = items_size[lctx->Rank()];
  size_t sender_size = items_size[lctx->NextRank()];

  YACL_ENFORCE(receiver_size == inputs.size());

  if ((sender_size == 0) || (receiver_size == 0)) {
    return {};
  }
  YACL_ENFORCE(sender_size <= receiver_size);

  size_t mask_size = sizeof(uint128_t);
  if (options.compress) {
    mask_size = ComputeTruncateSize(sender_size, receiver_size, options.ssp,
                                    options.malicious);
  }

  Rr22OprfReceiver oprf_receiver(kRr22OprfBinSize, options.ssp, options.mode,
                                 options.code_type, options.malicious);

  SPDLOG_INFO("out buffer begin");
  yacl::Buffer outputs_buffer(inputs.size() * sizeof(uint128_t));
  absl::Span<uint128_t> outputs =
      absl::MakeSpan((uint128_t*)(outputs_buffer.data()), inputs.size());
  SPDLOG_INFO("out buffer end");

  oprf_receiver.Recv(lctx, receiver_size, inputs, outputs, options.num_threads);

  SPDLOG_INFO("compute intersection begin, threads:{}", options.num_threads);

  std::vector<size_t> indices =
      GetIntersection(outputs, sender_size, lctx, options.num_threads,
                      options.compress, mask_size);

  SPDLOG_INFO("compute intersection end");

  return indices;
}

Rr22PsiOperator::Options Rr22PsiOperator::ParseConfig(
    const MemoryPsiConfig& config,
    const std::shared_ptr<yacl::link::Context>& lctx) {
  Options options;
  options.link_ctx = lctx;
  options.receiver_rank = config.receiver_rank();

  size_t thread_num = omp_get_num_procs();

  options.rr22_options.ssp = 40;
  options.rr22_options.num_threads = thread_num;
  options.rr22_options.compress = true;

  return options;
}

std::vector<std::string> Rr22PsiOperator::OnRun(
    const std::vector<std::string>& inputs) {
  std::vector<std::string> result;

  // Gather Items Size
  std::vector<size_t> items_size = AllGatherItemsSize(link_ctx_, inputs.size());
  size_t max_size = std::max(items_size[link_ctx_->Rank()],
                             items_size[link_ctx_->NextRank()]);

  // hash items to uint128_t
  std::vector<uint128_t> items_hash(inputs.size());

  SPDLOG_INFO("begin items hash");
  yacl::parallel_for(0, inputs.size(), [&](int64_t begin, int64_t end) {
    for (int64_t idx = begin; idx < end; ++idx) {
      items_hash[idx] = yacl::crypto::Blake3_128(inputs[idx]);
    }
  });
  SPDLOG_INFO("end items hash");

  // padding receiver's input to max_size
  if ((options_.receiver_rank == link_ctx_->Rank()) &&
      (inputs.size() < max_size)) {
    items_hash.resize(max_size);
    for (size_t idx = inputs.size(); idx < max_size; idx++) {
      items_hash[idx] = yacl::crypto::SecureRandU128();
    }
  }

  const auto psi_core_start = std::chrono::system_clock::now();

  if (options_.receiver_rank == link_ctx_->Rank()) {
    std::vector<size_t> rr22_psi_result = Rr22PsiReceiverInternal(
        options_.rr22_options, options_.link_ctx, items_hash);

    const auto psi_core_end = std::chrono::system_clock::now();
    const DurationMillis psi_core_duration = psi_core_end - psi_core_start;
    SPDLOG_INFO("rank: {}, psi_core_duration:{}", options_.link_ctx->Rank(),
                (psi_core_duration.count() / 1000));

    result.reserve(rr22_psi_result.size());

    for (auto index : rr22_psi_result) {
      result.push_back(inputs[index]);
    }
  } else {
    Rr22PsiSenderInternal(options_.rr22_options, options_.link_ctx, items_hash);

    const auto psi_core_end = std::chrono::system_clock::now();
    const DurationMillis psi_core_duration = psi_core_end - psi_core_start;
    SPDLOG_INFO("rank: {}, psi_core_duration:{}", options_.link_ctx->Rank(),
                (psi_core_duration.count() / 1000));
  }

  return result;
}

namespace {

std::unique_ptr<PsiBaseOperator> CreateFastOperator(
    const MemoryPsiConfig& config,
    const std::shared_ptr<yacl::link::Context>& lctx) {
  auto options = Rr22PsiOperator::ParseConfig(config, lctx);

  return std::make_unique<Rr22PsiOperator>(options);
}

std::unique_ptr<PsiBaseOperator> CreateLowCommOperator(
    const MemoryPsiConfig& config,
    const std::shared_ptr<yacl::link::Context>& lctx) {
  auto options = Rr22PsiOperator::ParseConfig(config, lctx);

  options.rr22_options.mode = rr22::Rr22PsiMode::LowCommMode;

  return std::make_unique<Rr22PsiOperator>(options);
}

std::unique_ptr<PsiBaseOperator> CreateMaliciousOperator(
    const MemoryPsiConfig& config,
    const std::shared_ptr<yacl::link::Context>& lctx) {
  auto options = Rr22PsiOperator::ParseConfig(config, lctx);

  options.rr22_options.mode = rr22::Rr22PsiMode::FastMode;

  options.rr22_options.malicious = true;
  options.rr22_options.code_type = yacl::crypto::CodeType::ExAcc7;

  return std::make_unique<Rr22PsiOperator>(options);
}

REGISTER_OPERATOR(RR22_FAST_PSI_2PC, CreateFastOperator);
REGISTER_OPERATOR(RR22_LOWCOMM_PSI_2PC, CreateLowCommOperator);

// malicious
REGISTER_OPERATOR(RR22_MALICIOUS_PSI_2PC, CreateMaliciousOperator);

}  // namespace

}  // namespace psi
