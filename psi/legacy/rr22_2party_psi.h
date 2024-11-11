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
#include <string>
#include <vector>

#include "psi/legacy/base_operator.h"
#include "psi/rr22/rr22_psi.h"

namespace psi {

class Rr22PsiOperator : public PsiBaseOperator {
 public:
  struct Options {
    std::shared_ptr<yacl::link::Context> link_ctx;
    size_t receiver_rank = 0;
    rr22::Rr22PsiOptions rr22_options = rr22::Rr22PsiOptions(40, 0, true);
  };

  static Options ParseConfig(const MemoryPsiConfig& config,
                             const std::shared_ptr<yacl::link::Context>& lctx);

  explicit Rr22PsiOperator(const Options& options)
      : PsiBaseOperator(options.link_ctx), options_(options) {}

  std::vector<std::string> OnRun(const std::vector<std::string>& inputs) final;

 private:
  Options options_;
};

class Rr22OprfSender : public rr22::Rr22Oprf {
 public:
  Rr22OprfSender(
      size_t bin_size, size_t ssp,
      rr22::Rr22PsiMode mode = rr22::Rr22PsiMode::FastMode,
      const yacl::crypto::CodeType& code_type = yacl::crypto::CodeType::ExAcc7,
      bool malicious = false)
      : Rr22Oprf(bin_size, ssp, mode, code_type, malicious) {
    if (malicious && mode == rr22::Rr22PsiMode::LowCommMode) {
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
  rr22::okvs::Baxos baxos_;
  rr22::okvs::Paxos<uint32_t> paxos_;

  // b = delta * a + c
  uint128_t delta_ = 0;
  yacl::Buffer b_;
};

class Rr22OprfReceiver : public rr22::Rr22Oprf {
 public:
  Rr22OprfReceiver(
      size_t bin_size, size_t ssp,
      rr22::Rr22PsiMode mode = rr22::Rr22PsiMode::FastMode,
      const yacl::crypto::CodeType& code_type = yacl::crypto::CodeType::ExAcc7,
      bool malicious = false)
      : Rr22Oprf(bin_size, ssp, mode, code_type, malicious) {
    if (malicious && mode == rr22::Rr22PsiMode::LowCommMode) {
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

}  // namespace psi
