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

#pragma once

#include <memory>
#include <vector>

#include "yacl/base/int128.h"
#include "yacl/kernel/algorithms/silent_vole.h"
#include "yacl/link/context.h"

#include "psi/rr22/okvs/baxos.h"

// Reference:
// Blazing Fast PSI from Improved OKVS and Subfield VOLE
// https://eprint.iacr.org/2022/320
// 5. SubField VOLE and PSI  Fig 11
//
// VOLE-PSI: Fast OPRF and Circuit-PSI from Vector-OLE
// https://eprint.iacr.org/2021/266.pdf
// 3.2 Malicious Secure Oblivious PRF.
namespace psi::rr22 {

enum class Rr22PsiMode {
  FastMode,
  LowCommMode,
};

class MocRr22VoleSender {
 public:
  explicit MocRr22VoleSender(uint128_t seed);

  void Send(const std::shared_ptr<yacl::link::Context>& lctx,
            absl::Span<uint128_t> c);

  void SendF64(const std::shared_ptr<yacl::link::Context>& lctx,
               absl::Span<uint128_t> c);

  uint128_t GetDelta() { return delta_; }

 private:
  uint128_t delta_;
  uint128_t seed_;
};

class MocRr22VoleReceiver {
 public:
  explicit MocRr22VoleReceiver(uint128_t seed);

  void Recv(const std::shared_ptr<yacl::link::Context>& lctx,
            absl::Span<uint128_t> a, absl::Span<uint128_t> b);

  void RecvF64(const std::shared_ptr<yacl::link::Context>& lctx,
               absl::Span<uint64_t> a, absl::Span<uint128_t> b);

  uint128_t seed_;
};

class Rr22Oprf {
 public:
  Rr22Oprf(
      size_t bin_size, size_t ssp, Rr22PsiMode mode = Rr22PsiMode::FastMode,
      const yacl::crypto::CodeType& code_type = yacl::crypto::CodeType::Silver5,
      bool malicious = false)
      : bin_size_(bin_size),
        ssp_(ssp),
        code_type_(code_type),
        mode_(mode),
        malicious_(malicious) {}

  uint64_t GetBinSize() { return bin_size_; }

  uint64_t GetSsp() { return ssp_; }

  Rr22PsiMode GetMode() { return mode_; }

  yacl::crypto::CodeType GetCodeType() { return code_type_; }

  size_t GetPaxosSize() { return paxos_size_; }

 protected:
  //
  uint64_t bin_size_ = 0;
  // ssp i.e.statistical security parameter.
  // must >= 30bit
  uint64_t ssp_ = 40;

  // Silver & ExAcc code
  yacl::crypto::CodeType code_type_;

  // fase or lowcomm mode
  Rr22PsiMode mode_;

  bool malicious_ = false;
  uint128_t w_ = 0;

  bool debug_ = false;

  size_t paxos_size_ = 0;
};

class Rr22OprfSender : public Rr22Oprf {
 public:
  Rr22OprfSender(
      size_t bin_size, size_t ssp, Rr22PsiMode mode = Rr22PsiMode::FastMode,
      const yacl::crypto::CodeType& code_type = yacl::crypto::CodeType::Silver5,
      bool malicious = false)
      : Rr22Oprf(bin_size, ssp, mode, code_type, malicious) {
    if (malicious && mode == Rr22PsiMode::LowCommMode) {
      YACL_THROW("RR22 malicious psi not support LowCommMode");
    }
  }

  void Send(const std::shared_ptr<yacl::link::Context>& lctx, size_t n,
            absl::Span<const uint128_t> inputs,
            absl::Span<uint128_t> hash_outputs, size_t num_threads);

  void SendFast(const std::shared_ptr<yacl::link::Context>& lctx, size_t n,
                absl::Span<const uint128_t> inputs,
                absl::Span<uint128_t> hash_outputs, size_t num_threads);

  void SendLowComm(const std::shared_ptr<yacl::link::Context>& lctx, size_t n,
                   absl::Span<const uint128_t> inputs,
                   absl::Span<uint128_t> hash_outputs, size_t num_threads);

  void HashInputMulDelta(absl::Span<const uint128_t> inputs,
                         absl::Span<uint128_t> hash_outputs);

  void Eval(absl::Span<const uint128_t> inputs, absl::Span<uint128_t> outputs,
            uint64_t num_threads = 0);

  void Eval(absl::Span<const uint128_t> inputs,
            absl::Span<const uint128_t> inputs_hash,
            absl::Span<uint128_t> outputs, uint64_t num_threads = 0);

 private:
  okvs::Baxos baxos_;
  okvs::Paxos<uint32_t> paxos_;

  // b = delta * a + c
  uint128_t delta_ = 0;
  yacl::Buffer b_;
};

class Rr22OprfReceiver : public Rr22Oprf {
 public:
  Rr22OprfReceiver(
      size_t bin_size, size_t ssp, Rr22PsiMode mode = Rr22PsiMode::FastMode,
      const yacl::crypto::CodeType& code_type = yacl::crypto::CodeType::Silver5,
      bool malicious = false)
      : Rr22Oprf(bin_size, ssp, mode, code_type, malicious) {
    if (malicious && mode == Rr22PsiMode::LowCommMode) {
      YACL_THROW("RR22 malicious psi not support LowCommMode");
    }
  }

  void Recv(const std::shared_ptr<yacl::link::Context>& lctx,
            size_t paxos_init_size, const std::vector<uint128_t>& inputs,
            absl::Span<uint128_t> outputs, size_t num_threads);

  void RecvFast(const std::shared_ptr<yacl::link::Context>& lctx,
                size_t paxos_init_size, const std::vector<uint128_t>& inputs,
                absl::Span<uint128_t> outputs, size_t num_threads);

  void RecvLowComm(const std::shared_ptr<yacl::link::Context>& lctx,
                   size_t paxos_init_size, const std::vector<uint128_t>& inputs,
                   absl::Span<uint128_t> outputs, size_t num_threads);

 private:
};

}  // namespace psi::rr22
