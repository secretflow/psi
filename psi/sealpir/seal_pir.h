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

#include "seal/seal.h"
#include "seal/util/polyarithsmallmod.h"
#include "yacl/base/byte_container_view.h"

#include "psi/sealpir/seal_pir_utils.h"

#include "psi/sealpir/serializable.pb.h"

namespace psi::sealpir {

struct PirParams {
  bool enable_symmetric;
  bool enable_batching;  // pack as many elements as possible into a BFV
                         // plaintext (recommended)
  bool enable_mswitching;
  uint64_t ele_num;
  uint64_t ele_size;
  uint64_t elements_per_plaintext;
  uint64_t num_of_plaintexts;
  uint32_t expansion_ratio;
  uint32_t dimension;
  std::vector<uint64_t> dimension_vec;
  uint32_t slot_cnt;
};

struct SealPirOptions {
  // RLWE polynomial degree
  uint32_t poly_modulus_degree;
  // db element number
  uint64_t element_number;
  // byte size of per element
  uint64_t element_size;
  // number of real query data
  uint64_t ind_degree = 0;
  // number of dimension
  uint32_t dimension = 0;
  // log2 of plaintext modulus
  uint32_t logt = 20;
};

class SealPir {
 public:
  using PirQuery = std::vector<std::vector<seal::Ciphertext>>;
  using PirReply = std::vector<seal::Ciphertext>;
  using Database = std::vector<seal::Plaintext>;

  explicit SealPir(const SealPirOptions &options) : options_(options) {
    // init some Seal Object and verify
    SetSealContext(options.poly_modulus_degree);

    if (options.ind_degree > 0) {
      SetPirParams(options.ind_degree, options.element_size, options.dimension);
    } else {
      SetPirParams(options.element_number, options.element_size,
                   options.dimension);
    }
  }
  void SetSealContext(size_t degree);
  void VerifyEncryptionParams();

  void SetPirParams(size_t ele_num, size_t ele_size, size_t d);
  void PrintPirParams();

  template <typename T>
  std::string SerializeSealObject(const T &object) {
    std::ostringstream output;
    object.save(output);
    return output.str();
  }

  template <typename T>
  T DeSerializeSealObject(const std::string &object_bytes,
                          bool safe_load = false) {
    T seal_object;
    std::istringstream object_input(object_bytes);
    if (safe_load) {
      seal_object.load(*context_, object_input);
    } else {
      seal_object.unsafe_load(*context_, object_input);
    }
    return seal_object;
  }

  std::string SerializePlaintexts(const std::vector<seal::Plaintext> &plains);

  std::vector<seal::Plaintext> DeSerializePlaintexts(
      const std::string &plaintext_bytes, bool safe_load = false);

  yacl::Buffer SerializeCiphertexts(
      const std::vector<seal::Ciphertext> &ciphers);

  std::vector<seal::Ciphertext> DeSerializeCiphertexts(
      const CiphertextsProto &ciphers_proto, bool safe_load = false);

  std::vector<seal::Ciphertext> DeSerializeCiphertexts(
      const yacl::Buffer &ciphers_buffer, bool safe_load = false);

  yacl::Buffer SerializeQuery(
      SealPirQueryProto *query_proto,
      const std::vector<std::vector<seal::Ciphertext>> &query_ciphers);

  yacl::Buffer SerializeQuery(
      const std::vector<std::vector<seal::Ciphertext>> &query_ciphers,
      size_t start_pos);

  std::vector<std::vector<seal::Ciphertext>> DeSerializeQuery(
      const yacl::Buffer &query_buffer, bool safe_load = false);

  std::vector<std::vector<seal::Ciphertext>> DeSerializeQuery(
      const SealPirQueryProto &query_proto, bool safe_load = false);
  const PirParams &GetPirParams() { return pir_params_; }
  const seal::EncryptionParameters &GetEncParams() { return *enc_params_; }

 protected:
  SealPirOptions options_;
  PirParams pir_params_;
  std::unique_ptr<seal::EncryptionParameters> enc_params_;

  std::unique_ptr<seal::SEALContext> context_;
  std::unique_ptr<seal::Evaluator> evaluator_;
  std::unique_ptr<seal::BatchEncoder> encoder_;
};

class SealPirServer : public SealPir {
 public:
  SealPirServer(const SealPirOptions &options,
                std::shared_ptr<IDbPlaintextStore> plaintext_store);

  void SetDatabase(const std::shared_ptr<IDbElementProvider> &db_provider);
  void SetDatabase(const std::vector<yacl::ByteContainerView> &db_vec);

  std::vector<seal::Ciphertext> ExpandQuery(const seal::Ciphertext &encrypted,
                                            uint64_t m, uint32_t client_id);

  PirReply GenerateReply(const PirQuery &query, uint32_t start_pos,
                         uint32_t client_id);

  void SetGaloisKey(uint32_t client_id, seal::GaloisKeys galkey);

  std::string SerializeDbPlaintext(int db_index = 0);
  void DeSerializeDbPlaintext(const std::string &db_serialize_bytes,
                              int db_index = 0);

  void SetOneCt(seal::Ciphertext one);

 private:
  std::vector<std::unique_ptr<std::vector<seal::Plaintext>>> db_vec_;
  std::shared_ptr<IDbPlaintextStore> plaintext_store_;
  bool is_db_preprocessed_;
  std::unordered_map<int, seal::GaloisKeys> galoisKeys_;

  seal::Ciphertext one_;

  void MultiplyPowerOfX(const seal::Ciphertext &encrypted,
                        seal::Ciphertext &destination, uint32_t index);
};

class SealPirClient : public SealPir {
 public:
  SealPirClient(const SealPirOptions &options);
  PirQuery GenerateQuery(uint64_t index);

  seal::Plaintext DecodeReply(PirReply &reply);
  std::vector<uint8_t> DecodeReply(PirReply &reply, uint64_t offset);

  seal::GaloisKeys GenerateGaloisKeys();

  uint64_t GetFVIndex(uint64_t element_index);
  uint64_t GetFVOffset(uint64_t element_index);

  seal::Plaintext Decrypt(seal::Ciphertext ct);

  std::vector<uint8_t> ExtractBytes(seal::Plaintext pt, uint64_t offset);

  seal::Ciphertext GetOne();

 private:
  std::unique_ptr<seal::Encryptor> encryptor_;
  std::unique_ptr<seal::Decryptor> decryptor_;
  std::unique_ptr<seal::KeyGenerator> keygen_;

  std::vector<uint64_t> indices_;
  std::vector<uint64_t> inverse_scales_;

  friend class SealPirServer;
};
}  // namespace psi::sealpir