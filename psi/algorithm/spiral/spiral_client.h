// Copyright 2024 Ant Group Co., Ltd.
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

#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

#include "yacl/base/int128.h"
#include "yacl/crypto/rand/rand.h"
#include "yacl/crypto/tools/prg.h"

#include "psi/algorithm/pir_interface/index_pir.h"
#include "psi/algorithm/spiral/discrete_gaussian.h"
#include "psi/algorithm/spiral/params.h"
#include "psi/algorithm/spiral/poly_matrix.h"
#include "psi/algorithm/spiral/public_keys.h"
#include "psi/algorithm/spiral/serialize.h"

#include "psi/algorithm/spiral/serializable.pb.h"

namespace psi::spiral {

struct SpiralQuery {
  // When ExpandQuery = true, ct_ contains one Regev ciphertext
  PolyMatrixRaw ct_;

  // we can use seed to compress Query
  uint128_t seed_;

  SpiralQuery() = default;

  bool operator==(const SpiralQuery& other) const {
    return this->ct_ == other.ct_ && this->seed_ == other.seed_;
  }

  yacl::Buffer Serialize();
  yacl::Buffer SerializeRng();
  std::string SerializeToStr();
  std::string SerializeRngToStr();

  static SpiralQuery Deserialize(const Params& params,
                                 const yacl::ByteContainerView& buffer);
  static SpiralQuery DeserializeRng(const Params& params,
                                    const yacl::ByteContainerView& buffer);
};

class SpiralClient : public psi::pir::IndexPirClient {
 public:
  explicit SpiralClient(Params params) : params_(std::move(params)) {
    Init();
    GenSecretKeys();  // Use random seed
  }
  explicit SpiralClient(Params params, uint128_t seed)
      : params_(std::move(params)) {
    Init();
    GenSecretKeys(seed);
  }

  SpiralClient(Params params, DatabaseMetaInfo database_info)
      : params_(std::move(params)), database_info_(database_info) {
    Init();

    size_t element_byte_len =
        std::min(database_info_.byte_size_per_row_, params_.MaxByteLenOfPt());

    partition_num_ =
        (database_info_.byte_size_per_row_ + params_.MaxByteLenOfPt() - 1) /
        params_.MaxByteLenOfPt();

    element_size_of_pt_ = params_.ElementSizeOfPt(element_byte_len);
    GenSecretKeys();  // Use random seed
  }

  PublicKeys GenPublicKeys() const;

  yacl::Buffer GeneratePksBuffer() const override {
    return SerializePublicKeys(params_, GenPublicKeys());
  }
  std::string GeneratePksString() const override {
    return SerializePublicKeysToStr(params_, GenPublicKeys());
  }

  SpiralQuery GenQuery(size_t raw_idx_target) const {
    YACL_ENFORCE(database_info_.rows_ > 0);
    YACL_ENFORCE_LT(raw_idx_target, database_info_.rows_);

    size_t pt_idx_target = raw_idx_target / element_size_of_pt_;
    return GenQueryInternal(pt_idx_target);
  }

  std::vector<uint8_t> DecodeResponse(
      const std::vector<PolyMatrixRaw>& response, size_t raw_idx_target) const;

  const PolyMatrixRaw& GetSkRegev() const { return sk_reg_; }

  const PolyMatrixRaw& GetSkGsw() const { return sk_gsw_; }

  const PolyMatrixRaw& GetSkRegevFull() const { return sk_reg_full_; }

  const PolyMatrixRaw& GetSkGswFull() const { return sk_gsw_full_; }

  bool GetSkInited() const { return sk_inited_; }

  const Params& GetParams() const { return params_; };

  const Params& GetQ2Params() const { return q2_params_; }

  ~SpiralClient() override = default;

  yacl::Buffer GenerateIndexQuery(uint64_t raw_idx) const override {
    auto query = GenQuery(raw_idx);

    return query.SerializeRng();
  }
  std::string GenerateIndexQueryStr(uint64_t raw_idx) const override {
    auto query = GenQuery(raw_idx);

    return query.SerializeRngToStr();
  }

  std::vector<uint8_t> DecodeIndexResponse(
      const yacl::ByteContainerView& response_buffer,
      uint64_t raw_idx) const override {
    auto response = DeserializeResponse(params_, response_buffer);

    return DecodeResponse(response, raw_idx);
  }

  pir::PirType GetPirType() const override { return pir::PirType::SPIRAL_PIR; }

  // protected:
  SpiralQuery GenQueryInternal(size_t pt_idx_target) const;

  PolyMatrixRaw DecodeResponseInternal(
      const std::vector<PolyMatrixRaw>& response) const;

  PolyMatrixRaw DecodeResponseInternal(const PolyMatrixRaw& response) const;

  PolyMatrixRaw GetFreshGswPublicKey(
      size_t m, yacl::crypto::Prg<uint64_t>& rng,
      yacl::crypto::Prg<uint64_t>& rng_pub) const;

  PolyMatrixNtt GetRegevSample(yacl::crypto::Prg<uint64_t>& rng,
                               yacl::crypto::Prg<uint64_t>& rng_pub) const;

  PolyMatrixNtt GetScaledRegevSample(yacl::crypto::Prg<uint64_t>& rng,
                                     yacl::crypto::Prg<uint64_t>& rng_pub,
                                     uint64_t scale) const;

  PolyMatrixNtt GetFreshRegevPublicKey(
      size_t m, yacl::crypto::Prg<uint64_t>& rng,
      yacl::crypto::Prg<uint64_t>& rng_pub) const;

  PolyMatrixNtt GetFreshScaledRegevPublicKey(
      size_t m, yacl::crypto::Prg<uint64_t>& rng,
      yacl::crypto::Prg<uint64_t>& rng_pub, uint64_t scale) const;

  PolyMatrixNtt DecryptMatrixRegev(const PolyMatrixNtt& a) const {
    auto sk_reg_full_ntt = ToNtt(GetParams(), sk_reg_full_);
    return Multiply(GetParams(), sk_reg_full_ntt, a);
  }

  PolyMatrixNtt DecryptMatrixGsw(const PolyMatrixNtt& a) const {
    auto sk_gsw_full_ntt = ToNtt(GetParams(), sk_gsw_full_);
    return Multiply(GetParams(), sk_gsw_full_ntt, a);
  }

  PolyMatrixNtt EncryptMatrixGsw(PolyMatrixNtt& ag,
                                 yacl::crypto::Prg<uint64_t>& rng,
                                 yacl::crypto::Prg<uint64_t>& rng_pub) const;

  PolyMatrixNtt EncryptMatrixRegev(PolyMatrixNtt& a,
                                   yacl::crypto::Prg<uint64_t>& rng,
                                   yacl::crypto::Prg<uint64_t>& rng_pub) const;

  PolyMatrixNtt EncryptMatrixScaledRegev(PolyMatrixNtt& a,
                                         yacl::crypto::Prg<uint64_t>& rng,
                                         yacl::crypto::Prg<uint64_t>& rng_pub,
                                         uint64_t scale) const;

 private:
  // core implementation
  void GenSecretKeys(yacl::crypto::Prg<uint64_t>& rng);

  void GenSecretKeys(uint128_t seed) {
    yacl::crypto::Prg<uint64_t> prg(seed);
    GenSecretKeys(prg);
  }

  void GenSecretKeys() { GenSecretKeys(yacl::crypto::SecureRandU128()); }

  void Init();

  std::vector<PolyMatrixNtt> GenExpansionParams(
      size_t num_exp, size_t m_exp, yacl::crypto::Prg<uint64_t>& rng,
      yacl::crypto::Prg<uint64_t>& rng_pub) const;

  // Paramss
  Params params_;

  // just for decode, the given moduli is q2
  Params q2_params_;

  PolyMatrixRaw sk_gsw_;

  PolyMatrixRaw sk_reg_;

  PolyMatrixRaw sk_gsw_full_;

  PolyMatrixRaw sk_reg_full_;

  DiscreteGaussian dg_;

  bool sk_inited_ = false;

  DatabaseMetaInfo database_info_;

  // element size per Regev plaintext, R_p^{n*n}
  size_t element_size_of_pt_ = 0;

  size_t partition_num_ = 1;
};

// util methods
void ReorientRegCiphertexts(const Params& params, std::vector<uint64_t>& out,
                            const std::vector<PolyMatrixNtt>& v_reg);

}  // namespace psi::spiral
