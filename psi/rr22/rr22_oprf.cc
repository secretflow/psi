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
#include <cstdint>
#include <future>
#include <string>
#include <vector>

#include "spdlog/spdlog.h"
#include "yacl/base/buffer.h"
#include "yacl/base/byte_container_view.h"
#include "yacl/crypto/rand/rand.h"
#include "yacl/crypto/tools/prg.h"
#include "yacl/crypto/tools/ro.h"
#include "yacl/math/galois_field/gf_intrinsic.h"
#include "yacl/utils/parallel.h"

#include "psi/rr22/davis_meyer_hash.h"
#include "psi/rr22/okvs/galois128.h"
#include "psi/rr22/rr22_utils.h"

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

std::vector<uint128_t> Rr22OprfSender::Send(
    const std::shared_ptr<yacl::link::Context>& lctx,
    const absl::Span<const uint128_t>& inputs) {
  if (mode_ == Rr22PsiMode::FastMode) {
    return SendFast(lctx, inputs);
  } else {
    return SendLowComm(lctx, inputs);
  }
}

void Rr22OprfSender::Init(const std::shared_ptr<yacl::link::Context>& lctx,
                          size_t init_size, size_t num_threads) {
  init_size_ = init_size;
  num_threads_ = num_threads;
  if (mode_ == Rr22PsiMode::FastMode) {
    uint128_t baxos_seed;
    SPDLOG_INFO("recv baxos seed...");

    yacl::Buffer baxos_seed_buf =
        lctx->Recv(lctx->NextRank(), fmt::format("recv baxos seed"));
    YACL_ENFORCE(baxos_seed_buf.size() == sizeof(uint128_t));

    SPDLOG_INFO("recv baxos seed finished");

    std::memcpy(&baxos_seed, baxos_seed_buf.data(), baxos_seed_buf.size());

    baxos_.Init(init_size_, bin_size_, kPaxosWeight, ssp_,
                okvs::PaxosParam::DenseType::GF128, baxos_seed);
    paxos_size_ = baxos_.size();
    SPDLOG_INFO("paxos_size:{}", paxos_size_);
    // vole send function
    yacl::crypto::SilentVoleSender vole_sender(code_type_, malicious_);
    size_t v_size = std::max<size_t>(256, baxos_.size());
    b_ = std::vector<uint128_t>(v_size, 0);
    absl::Span<uint128_t> b128_span = absl::MakeSpan(b_);

    SPDLOG_INFO("begin vole send");

    vole_sender.Send(lctx, b128_span);
    delta_ = vole_sender.GetDelta();

    SPDLOG_INFO("end vole send");
  } else if (mode_ == Rr22PsiMode::LowCommMode) {
    uint128_t paxos_seed;
    SPDLOG_INFO("recv paxos seed...");

    yacl::Buffer paxos_seed_buf =
        lctx->Recv(lctx->NextRank(), fmt::format("recv paxos seed"));
    YACL_ENFORCE(paxos_seed_buf.size() == sizeof(uint128_t));

    std::memcpy(&paxos_seed, paxos_seed_buf.data(), paxos_seed_buf.size());

    paxos_.Init(init_size_, kPaxosWeight, ssp_,
                okvs::PaxosParam::DenseType::Binary, paxos_seed);

    paxos_size_ = paxos_.size();

    // vole send function

    yacl::crypto::SilentVoleSender vole_sender(code_type_);
    size_t v_size = std::max<size_t>(256, paxos_.size());
    b_ = std::vector<uint128_t>(v_size, 0);
    absl::Span<uint128_t> b128_span = absl::MakeSpan(b_);

    SPDLOG_INFO("begin vole send");

    vole_sender.SfSend(lctx, b128_span);
    delta_ = vole_sender.GetDelta();

    SPDLOG_INFO("end vole send");
  } else {
    YACL_THROW("unsupported mode:{}", int(mode_));
  }
}

std::vector<uint128_t> Rr22OprfSender::SendFast(
    const std::shared_ptr<yacl::link::Context>& lctx,
    const absl::Span<const uint128_t>& inputs) {
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
  auto hash_inputs_proc = std::async([&] { return HashInputMulDelta(inputs); });

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
  auto paxos_solve_v = RecvChunked<uint128_t>(lctx, paxos_size_);
  SPDLOG_INFO("recv paxos solve finished. bytes:{}",
              paxos_solve_v.size() * sizeof(uint128_t));

  auto outputs = hash_inputs_proc.get();

  okvs::Galois128 delta_gf128(delta_);

  SPDLOG_INFO("begin b xor delta a");
  yacl::parallel_for(0, paxos_solve_v.size(), [&](int64_t begin, int64_t end) {
    for (int64_t idx = begin; idx < end; ++idx) {
      b_[idx] = b_[idx] ^ (delta_gf128 * paxos_solve_v[idx]).get<uint128_t>(0);
    }
  });

  SPDLOG_INFO("end b xor delta a");
  return outputs;
}

std::vector<uint128_t> Rr22OprfSender::SendLowComm(
    const std::shared_ptr<yacl::link::Context>& lctx,
    const absl::Span<const uint128_t>& inputs) {
  auto hash_outputs = HashInputMulDelta(inputs);

  SPDLOG_INFO("recv paxos solve ...");
  auto paxos_solve_v = RecvChunked<uint64_t>(lctx, paxos_size_);
  SPDLOG_INFO("recv paxos solve finished. bytes:{}",
              paxos_solve_v.size() * sizeof(uint64_t));

  absl::Span<uint64_t> paxos_solve_u64 = absl::MakeSpan(paxos_solve_v);

  SPDLOG_INFO("paxos_solve_u64 size:{}", paxos_solve_u64.size());

  okvs::Galois128 delta_gf128(delta_);

  absl::Span<uint128_t> b128_span =
      absl::MakeSpan(reinterpret_cast<uint128_t*>(b_.data()),
                     std::max<size_t>(256, paxos_.size()));
  for (size_t i = 0; i < paxos_solve_u64.size(); ++i) {
    // Delta * (A - P), note that here is GF64 * GF128 = GF128
    b128_span[i] =
        b128_span[i] ^ (delta_gf128 * paxos_solve_u64[i]).get<uint128_t>(0);
  }
  return hash_outputs;
}

std::vector<uint128_t> Rr22OprfSender::HashInputMulDelta(
    const absl::Span<const uint128_t>& inputs) {
  std::vector<uint128_t> hash_outputs(inputs.size());
  okvs::Galois128 delta_gf128(delta_);
  okvs::AesCrHash aes_crhash(kAesHashSeed);

  if (mode_ == Rr22PsiMode::FastMode) {
    yacl::parallel_for(0, inputs.size(), [&](int64_t begin, int64_t end) {
      for (int64_t i = begin; i < end; ++i) {
        hash_outputs[i] =
            (delta_gf128 * aes_crhash.Hash(inputs[i])).get<uint128_t>(0);
      }
    });

  } else if (mode_ == Rr22PsiMode::LowCommMode) {
    yacl::parallel_for(0, inputs.size(), [&](int64_t begin, int64_t end) {
      for (int64_t i = begin; i < end; ++i) {
        hash_outputs[i] =
            (delta_gf128 *
             yacl::DecomposeUInt128(aes_crhash.Hash(inputs[i])).second)
                .get<uint128_t>(0);
      }
    });
  } else {
    YACL_THROW("unsupported rr22 psi mode");
  }
  return hash_outputs;
}

std::vector<uint128_t> Rr22OprfSender::Eval(
    const absl::Span<const uint128_t>& inputs) {
  SPDLOG_INFO("paxos decode ...");

  YACL_ENFORCE(b_.size() > 0, "Must use Send() first");
  std::vector<uint128_t> outputs(inputs.size());
  absl::Span<uint128_t> outputs_span = absl::MakeSpan(outputs);

  absl::Span<uint128_t> b128_span =
      absl::MakeSpan(reinterpret_cast<uint128_t*>(b_.data()), paxos_size_);

  if (mode_ == Rr22PsiMode::FastMode) {
    baxos_.Decode(inputs, outputs_span, b128_span, num_threads_);
  } else if (mode_ == Rr22PsiMode::LowCommMode) {
    paxos_.Decode(inputs, outputs_span, b128_span);
  } else {
    YACL_THROW("unsupported rr22 psi mode");
  }
  SPDLOG_INFO("paxos decode finished");
  b_.clear();

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
        // delta_gf128 * h is GF128 * GF64
        outputs[idx] = outputs[idx] ^ (delta_gf128 * h).get<uint128_t>(0);
      }
    });
  }
  if (malicious_) {
    DavisMeyerHash(outputs_span, inputs, outputs_span);
  } else {
    aes_crhash.Hash(outputs_span, outputs_span);
  }
  return outputs;
}

std::vector<uint128_t> Rr22OprfSender::Eval(
    const absl::Span<const uint128_t>& inputs,
    absl::Span<const uint128_t> inputs_hash) {
  YACL_ENFORCE(b_.size() > 0, "Must use Send() first");
  std::vector<uint128_t> outputs(inputs.size());
  auto outputs_span = absl::MakeSpan(outputs);
  absl::Span<uint128_t> b128_span =
      absl::MakeSpan(reinterpret_cast<uint128_t*>(b_.data()), paxos_size_);

  SPDLOG_INFO("paxos decode (mode:{}) ...",
              mode_ == Rr22PsiMode::FastMode ? "Fast" : "LowComm");

  if (mode_ == Rr22PsiMode::FastMode) {
    baxos_.Decode(inputs, outputs_span, b128_span, num_threads_);

  } else if (mode_ == Rr22PsiMode::LowCommMode) {
    paxos_.Decode(inputs, outputs_span, b128_span);
  } else {
    YACL_THROW("unsupported rr22 psi mode");
  }
  SPDLOG_INFO("paxos decode finished");
  b_.clear();

  yacl::parallel_for(0, inputs.size(), [&](int64_t begin, int64_t end) {
    for (int64_t idx = begin; idx < end; ++idx) {
      outputs[idx] = outputs[idx] ^ inputs_hash[idx];

      if (malicious_) {
        outputs[idx] = outputs[idx] ^ w_;
      }
    }
  });

  if (malicious_) {
    DavisMeyerHash(outputs_span, inputs, outputs_span);
  } else {
    okvs::AesCrHash aes_crhash(kAesHashSeed);

    aes_crhash.Hash(outputs_span, outputs_span);
  }
  return outputs;
}

void Rr22OprfReceiver::Init(const std::shared_ptr<yacl::link::Context>& lctx,
                            size_t init_size, size_t num_threads) {
  num_threads_ = num_threads;
  if (mode_ == Rr22PsiMode::FastMode) {
    uint128_t baxos_seed = 1;
    yacl::ByteContainerView paxos_seed_buf(&baxos_seed, sizeof(uint128_t));

    lctx->SendAsyncThrottled(lctx->NextRank(), paxos_seed_buf,
                             fmt::format("send baxos_seed_buf"));
    baxos_.Init(init_size, bin_size_, kPaxosWeight, ssp_,
                okvs::PaxosParam::DenseType::GF128, baxos_seed);
    paxos_size_ = baxos_.size();
    SPDLOG_INFO("baxos_size:{}", paxos_size_);
    // c + b = a * delta
    // vole recv
    yacl::crypto::SilentVoleReceiver vole_receiver(code_type_, malicious_);
    size_t v_size = std::max<size_t>(256, baxos_.size());
    a_ = std::vector<uint128_t>(v_size, 0);
    c_ = std::vector<uint128_t>(v_size, 0);

    SPDLOG_INFO("begin vole recv");
    vole_receiver.Recv(lctx, absl::MakeSpan(a_), absl::MakeSpan(c_));
    SPDLOG_INFO("end vole recv");
  } else if (mode_ == Rr22PsiMode::LowCommMode) {
    uint128_t paxos_seed = yacl::crypto::SecureRandU128();
    yacl::ByteContainerView paxos_seed_buf(&paxos_seed, sizeof(uint128_t));

    lctx->SendAsyncThrottled(lctx->NextRank(), paxos_seed_buf,
                             fmt::format("send paxos_seed_buf"));
    // here we must use DenseType::Binary for supporting EncodeU64, which
    // should used in LowComm
    paxos_.Init(init_size, kPaxosWeight, ssp_,
                okvs::PaxosParam::DenseType::Binary, paxos_seed);
    paxos_size_ = paxos_.size();

    // vole recv function
    SPDLOG_INFO("use SilentVoleReceiver");
    yacl::crypto::SilentVoleReceiver vole_receiver(code_type_);
    size_t v_size = std::max<size_t>(256, paxos_.size());
    a64_ = std::vector<uint64_t>(v_size, 0);
    c_ = std::vector<uint128_t>(v_size, 0);

    SPDLOG_INFO("begin vole recv");
    vole_receiver.SfRecv(lctx, absl::MakeSpan(a64_), absl::MakeSpan(c_));
    SPDLOG_INFO("end vole recv");

  } else {
    YACL_THROW("unsupported mode:{}", int(mode_));
  }
}

std::vector<uint128_t> Rr22OprfReceiver::Recv(
    const std::shared_ptr<yacl::link::Context>& lctx,
    const absl::Span<const uint128_t>& inputs) {
  if (mode_ == Rr22PsiMode::FastMode) {
    return RecvFast(lctx, inputs);
  } else {
    return RecvLowComm(lctx, inputs);
  }
}

std::vector<uint128_t> Rr22OprfReceiver::RecvFast(
    const std::shared_ptr<yacl::link::Context>& lctx,
    const absl::Span<const uint128_t>& inputs) {
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

  okvs::AesCrHash aes_crhash(kAesHashSeed);
  std::vector<uint128_t> outputs(inputs.size(), 0);
  auto outputs_span = absl::MakeSpan(outputs);
  aes_crhash.Hash(inputs, absl::MakeSpan(outputs_span));
  SPDLOG_INFO("solve begin");
  std::vector<uint128_t> p128_v(baxos_.size(), 0);
  auto p128_span = absl::MakeSpan(p128_v);
  baxos_.Solve(inputs, outputs_span, p128_span, nullptr, num_threads_);
  SPDLOG_INFO("solve end");

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
    baxos_.Decode(inputs, outputs_span,
                  absl::MakeSpan(c_.data(), baxos_.size()), num_threads_);
    c_.clear();
    if (malicious_) {
      for (size_t i = 0; i < outputs.size(); ++i) {
        outputs[i] = outputs[i] ^ w;
      }
    }

    if (malicious_) {
      SPDLOG_INFO("call Davis-Meyer hash");
      DavisMeyerHash(outputs_span, inputs, outputs_span);
    } else {
      SPDLOG_INFO("call aes crhash");
      aes_crhash.Hash(outputs_span, outputs_span);
    }
    SPDLOG_INFO("end compute self oprf");
  });

  SPDLOG_INFO("begin p xor a");

  yacl::parallel_for(0, p128_span.size(), [&](int64_t begin, int64_t end) {
    for (int64_t idx = begin; idx < end; ++idx) {
      p128_span[idx] = p128_span[idx] ^ a_[idx];
    }
  });
  a_.clear();

  SPDLOG_INFO("end p xor a");

  SendChunked(lctx, p128_span);
  oprf_eval_proc.get();
  return outputs;
}

std::vector<uint128_t> Rr22OprfReceiver::RecvLowComm(
    const std::shared_ptr<yacl::link::Context>& lctx,
    const absl::Span<const uint128_t>& inputs) {
  okvs::AesCrHash aes_crhash(kAesHashSeed);
  std::vector<uint128_t> outputs(inputs.size());
  auto outputs_span = absl::MakeSpan(outputs);
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

  std::vector<uint64_t> p64_v(paxos_.size(), 0);
  absl::Span<uint64_t> p64_span = absl::MakeSpan(p64_v);

  SPDLOG_INFO("solve begin");
  paxos_.SetInput(inputs);
  paxos_.EncodeU64(outputs_u64_span, p64_span, nullptr);
  SPDLOG_INFO("solve end");

  auto oprf_eval_proc = std::async([&] {
    SPDLOG_INFO("begin receiver oprf");
    paxos_.Decode(inputs, outputs_span,
                  absl::MakeSpan(c_.data(), paxos_.size()));
    c_.clear();
    // oprf end output
    aes_crhash.Hash(outputs_span, outputs_span);
    SPDLOG_INFO("end receiver oprf");
  });

  SPDLOG_INFO("begin p xor a");

  for (size_t i = 0; i < p64_span.size(); ++i) {
    p64_span[i] ^= a64_[i];
  }
  a64_.clear();
  SPDLOG_INFO("end p xor a");

  SendChunked(lctx, p64_span);
  oprf_eval_proc.get();
  return outputs;
}

}  // namespace psi::rr22
