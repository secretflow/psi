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

#include <cassert>
#include <cmath>
#include <string>
#include <vector>

#include "google/protobuf/io/zero_copy_stream_impl.h"
#include "google/protobuf/util/delimited_message_util.h"
#include "seal/seal.h"
#include "seal/util/polyarithsmallmod.h"
#include "yacl/base/byte_container_view.h"

#include "psi/algorithm/pir_interface/index_pir.h"
#include "psi/algorithm/pir_interface/pir_db.h"
#include "psi/algorithm/sealpir/seal_pir_utils.h"

#include "psi/algorithm/sealpir/serializable.pb.h"

namespace psi::sealpir {

// user-defined options
struct SealPirOptions {
  // RLWE polynomial degree
  uint32_t poly_modulus_degree = 4096;
  // db element number
  uint64_t element_number = 0;
  // byte size of per element
  uint64_t element_size = 0;
  // number of dimension
  uint32_t dimension = 2;
  // log2 of plaintext modulus
  uint32_t logt = 20;
};

class SealPir {
 public:
  // we can construct SealPirParams from SealPirOptions
  struct SealPirParams {
    uint64_t ele_num = 0;
    uint64_t ele_size = 0;
    uint64_t elements_per_plaintext = 0;
    uint64_t num_of_plaintexts = 0;
    uint64_t partition_size = 0;
    uint64_t partition_num = 1;
    uint32_t expansion_ratio = 0;
    uint32_t dimension = 2;
    std::vector<uint64_t> dimension_vec;
    uint32_t slot_cnt = 0;
    uint32_t logt = 0;
    bool enable_symmetric = true;
    bool enable_batching = true;
    bool enable_mswitching = true;

    std::string ToString() {
      std::ostringstream ss;
      ss << "PirParams: "
         << "\n";
      ss << "database rows: " << ele_num << ", each row length: " << ele_size
         << " bytes";
      ss << ", poly degree: " << slot_cnt << ", logt: " << logt
         << ", number of plaintexts: " << num_of_plaintexts;
      ss << ", dimension:{";
      for (size_t i = 0; i < dimension_vec.size(); ++i) {
        ss << dimension_vec[i];
        ss << (i + 1 == dimension_vec.size() ? "}" : ", ");
      }
      return ss.str();
    }
  };

  using PirQuery = std::vector<std::vector<seal::Ciphertext>>;
  // we use the seal::Serializable<seal::Ciphertext>> to compress the query
  using PirSeededQuery = std::vector<std::vector<std::string>>;
  using PirResponse = std::vector<std::vector<seal::Ciphertext>>;
  using Database = std::vector<seal::Plaintext>;

  explicit SealPir(const SealPirOptions &options) {
    // init some Seal Object and verify
    SetSealContext(options.poly_modulus_degree, options.logt);
    SetPirParams(options.element_number, options.element_size,
                 options.dimension);
  }

  SealPir(SealPir &pir) {
    pir_params_ = pir.pir_params_;
    enc_params_ = std::move(pir.enc_params_);
    context_ = std::move(pir.context_);
    evaluator_ = std::move(pir.evaluator_);
    encoder_ = std::move(pir.encoder_);
  }

  void SetSealContext(size_t poly_modulus_degree, size_t logt);
  void SetPirParams(size_t ele_num, size_t ele_size, size_t dimension);
  void PrintPirParams() const;

  template <typename T>
  std::string SerializeSealObject(const T &object) const {
    std::ostringstream output;
    object.save(output);
    return output.str();
  }

  template <typename T>
  T DeSerializeSealObject(const std::string &object_bytes,
                          bool safe_load = false) const {
    T seal_object;
    std::istringstream object_input(object_bytes);
    if (safe_load) {
      seal_object.load(*context_, object_input);
    } else {
      seal_object.unsafe_load(*context_, object_input);
    }
    return seal_object;
  }

  std::string SerializePlaintexts(
      const std::vector<seal::Plaintext> &plains) const;

  std::vector<seal::Plaintext> DeSerializePlaintexts(
      const std::string &plaintext_bytes, bool safe_load = false) const;

  yacl::Buffer SerializeCiphertexts(
      const std::vector<seal::Ciphertext> &ciphers) const;
  std::vector<seal::Ciphertext> DeSerializeCiphertexts(
      const yacl::Buffer &ciphers_buffer, bool safe_load = false) const;

  std::vector<seal::Ciphertext> DeSerializeCiphertexts(
      const CiphertextsProto &ciphers_proto, bool safe_load = false) const;

  yacl::Buffer SerializeResponse(
      SealPirResponseProto *response_proto,
      const std::vector<std::vector<seal::Ciphertext>> &response_ciphers) const;
  yacl::Buffer SerializeResponse(
      const std::vector<std::vector<seal::Ciphertext>> &response_ciphers) const;
  std::vector<std::vector<seal::Ciphertext>> DeSerializeResponse(
      const yacl::Buffer &response_buffer, bool safe_load = false) const;
  std::vector<std::vector<seal::Ciphertext>> DeSerializeResponse(
      const SealPirResponseProto &response_proto, bool safe_load = false) const;

  yacl::Buffer SerializeQuery(
      SealPirQueryProto *query_proto,
      const std::vector<std::vector<seal::Ciphertext>> &query_ciphers) const;
  yacl::Buffer SerializeQuery(
      const std::vector<std::vector<seal::Ciphertext>> &query_ciphers) const;

  yacl::Buffer SerializeSeededQuery(
      const std::vector<std::vector<std::string>> &query_ciphers) const;
  yacl::Buffer SerializeSeededQuery(
      SealPirQueryProto *query_proto,
      const std::vector<std::vector<std::string>> &query_ciphers) const;

  std::string SerializeCiphertextsToStr(
      const std::vector<seal::Ciphertext> &ciphers) const;
  std::vector<seal::Ciphertext> DeSerializeCiphertexts(
      const std::string &ciphers_buffer, bool safe_load = false) const;
  std::string SerializeQueryToStr(
      SealPirQueryProto *query_proto,
      const std::vector<std::vector<seal::Ciphertext>> &query_ciphers) const;
  std::string SerializeQueryToStr(
      const std::vector<std::vector<seal::Ciphertext>> &query_ciphers) const;
  std::string SerializeSeededQueryToStr(
      const std::vector<std::vector<std::string>> &query_ciphers) const;
  std::string SerializeSeededQueryToStr(
      SealPirQueryProto *query_proto,
      const std::vector<std::vector<std::string>> &query_ciphers) const;
  std::vector<std::vector<seal::Ciphertext>> DeSerializeQuery(
      const yacl::Buffer &query_buffer, bool safe_load = false) const;
  std::string SerializeResponseToStr(
      SealPirResponseProto *response_proto,
      const std::vector<std::vector<seal::Ciphertext>> &response_ciphers) const;
  std::string SerializeResponseToStr(
      const std::vector<std::vector<seal::Ciphertext>> &response_ciphers) const;
  std::vector<std::vector<seal::Ciphertext>> DeSerializeResponse(
      const std::string &response_buffer, bool safe_load = false) const;
  std::vector<std::vector<seal::Ciphertext>> DeSerializeQuery(
      const std::string &query_buffer, bool safe_load = false) const;
  std::vector<std::vector<seal::Ciphertext>> DeSerializeQuery(
      const SealPirQueryProto &query_proto, bool safe_load = false) const;

  const SealPirParams &GetPirParams() const { return pir_params_; }
  const seal::EncryptionParameters &GetEncParams() const {
    return *enc_params_;
  }

 protected:
  SealPirParams pir_params_;

  std::unique_ptr<seal::EncryptionParameters> enc_params_;
  std::unique_ptr<seal::SEALContext> context_;
  std::unique_ptr<seal::Evaluator> evaluator_;
  std::unique_ptr<seal::BatchEncoder> encoder_;
};

class SealPirServer : public SealPir, public psi::pir::IndexPirDataBase {
 public:
  explicit SealPirServer(const SealPirOptions &options);
  ~SealPirServer() override = default;

  SealPirServer(SealPirServer &server)
      : SealPir(server),
        psi::pir::IndexPirDataBase(psi::pir::PirType::SEAL_PIR) {
    for (uint64_t i = 0; i < db_vec_.size(); ++i) {
      db_vec_[i] = std::move(server.db_vec_[i]);
    }
  }

  void GenerateFromRawData(const psi::pir::RawDatabase &raw_data) override;

  bool DbSeted() const override { return db_seted_; }

  void GenerateFromRawData(const std::vector<yacl::ByteContainerView> &db_vec);
  void GenerateFromRawData(std::vector<std::vector<uint8_t>> raw_database) {
    psi::pir::RawDatabase raw_db(std::move(raw_database));
    GenerateFromRawData(raw_db);
  }

  SealPirServerProto SerializeToProto() const;
  static std::unique_ptr<SealPirServer> DeserializeFromProto(
      const SealPirServerProto &proto);

  void Dump(std::ostream &output) const override;

  static std::unique_ptr<SealPirServer> Load(
      google::protobuf::io::FileInputStream &input);

  PirResponse GenerateResponse(
      const std::vector<std::vector<seal::Ciphertext>> &query,
      const seal::GaloisKeys &key) const;

  yacl::Buffer Response(const yacl::Buffer &query_buffer,
                        const yacl::Buffer &pks_buffer) const override;
  std::string Response(const std::string &query_buffer,
                       const std::string &pks_buffer) const override;

  void SetGaloisKey(uint32_t client_id, const seal::GaloisKeys &galkey);
  void SetGaloisKey(uint32_t client_id, const yacl::Buffer &galkey);
  void SetGaloisKey(uint32_t client_id, const std::string &galkey);

  std::string SerializeDbPlaintext(int db_index = 0) const;
  void DeSerializeDbPlaintext(const std::string &db_serialize_bytes,
                              int db_index = 0);

 private:
  std::vector<std::unique_ptr<std::vector<seal::Plaintext>>> db_vec_;

  std::vector<std::shared_ptr<IDbPlaintextStore>> plaintext_store_;

  bool db_seted_ = false;
  bool galois_keys_seted_ = false;

  std::unordered_map<uint32_t, seal::GaloisKeys> galois_keys_;

  void MultiplyPowerOfX(const seal::Ciphertext &encrypted,
                        seal::Ciphertext &destination, uint32_t index) const;

  std::vector<seal::Ciphertext> ExpandQuery(const seal::Ciphertext &encrypted,
                                            uint64_t m,
                                            uint32_t client_id) const;
  std::vector<seal::Ciphertext> ExpandQuery(const seal::Ciphertext &encrypted,
                                            uint64_t m,
                                            const seal::GaloisKeys &key) const;
};

class SealPirClient : public SealPir, public psi::pir::IndexPirClient {
 public:
  explicit SealPirClient(const SealPirOptions &options);
  ~SealPirClient() override = default;

  // get Pliantext of fhe index from the index of raw database
  uint64_t GetPtIndex(uint64_t raw_idx) const;

  // get Plaintext of fhe offset from the index of raw database
  uint64_t GetPtOffset(uint64_t raw_idx) const;

  PirQuery GenerateQuery(uint64_t pt_idx) const;

  pir::PirType GetPirType() const override { return pir::PirType::SEAL_PIR; }

  PirSeededQuery GenerateSeededQuery(uint64_t pt_idx) const;

  yacl::Buffer GenerateIndexQuery(uint64_t raw_idx) const override;
  std::string GenerateIndexQueryStr(uint64_t raw_idx) const override;

  seal::GaloisKeys GenerateGaloisKeys() const;

  yacl::Buffer GeneratePksBuffer() const override;

  std::string GeneratePksString() const override;

  std::vector<uint8_t> DecodeResponse(PirResponse &response,
                                      uint64_t raw_idx) const;
  std::vector<uint8_t> DecodeIndexResponse(const yacl::Buffer &response_buffer,
                                           uint64_t raw_idx) const override;

  std::vector<uint8_t> DecodeIndexResponse(const std::string &response_buffer,
                                           uint64_t raw_idx) const override;

 private:
  std::vector<seal::Plaintext> DecodeResponse(PirResponse &response) const;

  std::unique_ptr<seal::Encryptor> encryptor_;
  std::unique_ptr<seal::Decryptor> decryptor_;
  std::unique_ptr<seal::KeyGenerator> keygen_;

  std::vector<uint8_t> ExtractBytes(seal::Plaintext pt, uint64_t offset,
                                    uint64_t loc_ele_size) const;
};
}  // namespace psi::sealpir
