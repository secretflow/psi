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

#include "psi/sealpir/seal_pir.h"

#include <memory.h>

#include <algorithm>
#include <functional>
#include <span>
#include <utility>

#include "spdlog/spdlog.h"
#include "yacl/base/exception.h"
#include "yacl/utils/parallel.h"

using namespace std;
using namespace seal;

namespace psi::sealpir {
namespace {
uint64_t ElementsPerPtxt(uint32_t logt, uint64_t N, uint64_t ele_size) {
  uint64_t coeffs_per_element = ceil((ele_size * 8) / (double)logt);
  uint64_t elements_per_plaintext = N / coeffs_per_element;
  YACL_ENFORCE_GT(elements_per_plaintext, 0UL);
  return elements_per_plaintext;
}

uint64_t PlaintextsPerDb(uint32_t logt, uint64_t N, uint64_t ele_num,
                         uint64_t ele_size) {
  uint64_t elements_per_plaintext = ElementsPerPtxt(logt, N, ele_size);
  return ceil((double)ele_num / elements_per_plaintext);
}

// "Note that Microsoft SEAL stores each polynomial in the ciphertext
// modulo all of the K primes in the coefficient modulus."
uint32_t ComputeExpansionRatio(seal::EncryptionParameters params) {
  uint32_t expansion_ratio = 0;
  uint32_t logt = log2(params.plain_modulus().value());
  for (uint32_t i = 0; i < params.coeff_modulus().size(); ++i) {
    double logqi = log2(params.coeff_modulus()[i].value());
    expansion_ratio += ceil(logqi / logt);
  }
  YACL_ENFORCE(expansion_ratio > 0, "expansion_ratio must be greater than 0");
  return expansion_ratio;
}
uint64_t CoefficientsPerElement(uint32_t logt, uint64_t ele_size) {
  return ceil((ele_size * 8) / (double)logt);
}

vector<uint64_t> GetDimensions(uint64_t num_of_plaintexts, uint32_t d) {
  YACL_ENFORCE_GT(d, 0UL);
  YACL_ENFORCE_GT(num_of_plaintexts, 0UL);

  uint64_t root =
      max(static_cast<uint64_t>(2),
          static_cast<uint64_t>(floor(pow(num_of_plaintexts, 1.0 / d))));

  vector<uint64_t> dimension_vec(d, root);

  for (uint32_t i = 0; i < d; ++i) {
    if (accumulate(dimension_vec.begin(), dimension_vec.end(),
                   static_cast<uint64_t>(1),
                   multiplies<uint64_t>()) > num_of_plaintexts) {
      break;
    }
    dimension_vec[i] += 1;
  }

  uint64_t prod = accumulate(dimension_vec.begin(), dimension_vec.end(),
                             static_cast<uint64_t>(1), multiplies<uint64_t>());
  YACL_ENFORCE_GE(prod, num_of_plaintexts);

  return dimension_vec;
}

vector<uint64_t> BytesToCoeffs(uint32_t logt, const gsl::span<uint8_t> bytes) {
  uint32_t ele_size = bytes.size();
  uint64_t size_out = CoefficientsPerElement(logt, ele_size);
  vector<uint64_t> output(size_out);

  uint32_t room = logt;
  uint64_t *target = &output[0];

  for (uint32_t i = 0; i < ele_size; ++i) {
    uint8_t src = bytes[i];
    uint32_t rest = 8;
    while (rest) {
      if (room == 0) {
        target++;
        room = logt;
      }

      uint32_t shift = min(rest, room);
      *target = *target << shift;
      *target = *target | (src >> (8 - shift));
      src = src << shift;
      room -= shift;
      rest -= shift;
    }
  }

  *target = *target << room;
  return output;
}

void CoeffsToBytes(uint32_t logt, const vector<uint64_t> &coeffs,
                   vector<uint8_t> &output, uint32_t ele_size) {
  uint32_t room = 8;
  uint32_t j = 0;
  uint8_t *target = output.data();
  uint32_t bits_left = ele_size * 8;

  for (uint32_t i = 0; i < coeffs.size(); ++i) {
    if (bits_left == 0) {
      bits_left = ele_size * 8;
    }

    uint64_t src = coeffs[i];
    uint32_t rest = min(logt, bits_left);
    while (rest && j < output.size()) {
      uint32_t shift = min(rest, room);

      target[j] = target[j] << shift;
      target[j] = target[j] | (src >> (logt - shift));

      src = src << shift;
      room -= shift;
      rest -= shift;
      bits_left -= shift;

      if (room == 0) {
        ++j;
        room = 8;
      }
    }
  }
}
vector<uint64_t> ComputeIndices(uint64_t desiredIndex,
                                vector<uint64_t> dimension_vec) {
  vector<uint64_t> result;
  uint64_t j = desiredIndex;

  uint64_t prod = accumulate(dimension_vec.begin(), dimension_vec.end(), 1,
                             multiplies<uint64_t>());

  for (uint32_t i = 0; i < dimension_vec.size(); ++i) {
    prod /= dimension_vec[i];
    uint64_t ji = j / prod;
    result.push_back(ji);
    j %= prod;
  }

  return result;
}

vector<seal::Plaintext> DecomposeToPlaintexts(seal::EncryptionParameters params,
                                              const seal::Ciphertext &ct) {
  const auto N = params.poly_modulus_degree();
  const auto coeff_mod_count = params.coeff_modulus().size();
  const uint32_t logt = log2(params.plain_modulus().value());
  const uint64_t pt_bitmask = (1ULL << logt) - 1;

  vector<seal::Plaintext> result(ComputeExpansionRatio(params) * ct.size());
  auto pt_iter = result.begin();

  for (uint32_t poly_index = 0; poly_index < ct.size(); ++poly_index) {
    for (uint32_t coeff_mod_index = 0; coeff_mod_index < coeff_mod_count;
         ++coeff_mod_index) {
      const double coeff_bit_size =
          log2(params.coeff_modulus()[coeff_mod_index].value());
      const size_t local_expansion_ratio = ceil(coeff_bit_size / logt);
      size_t shift = 0;

      for (size_t i = 0; i < local_expansion_ratio; ++i) {
        pt_iter->resize(N);
        for (size_t j = 0; j < N; ++j) {
          (*pt_iter)[j] =
              (ct.data(poly_index)[coeff_mod_index * N + j] >> shift) &
              pt_bitmask;
        }

        ++pt_iter;
        shift += logt;
      }
    }
  }
  return result;
}

void ComposeToCiphertext(seal::EncryptionParameters params,
                         vector<seal::Plaintext>::const_iterator pt_iter,
                         const size_t ct_poly_count, seal::Ciphertext &ct) {
  const auto N = params.poly_modulus_degree();
  const auto coeff_mod_count = params.coeff_modulus().size();
  const uint32_t logt = floor(log2(params.plain_modulus().value()));

  ct.resize(ct_poly_count);
  for (uint32_t poly_index = 0; poly_index < ct_poly_count; ++poly_index) {
    for (uint32_t coeff_mod_index = 0; coeff_mod_index < coeff_mod_count;
         ++coeff_mod_index) {
      const double coeff_bit_size =
          log2(params.coeff_modulus()[coeff_mod_index].value());
      const size_t local_expansion_ratio = ceil(coeff_bit_size / logt);
      size_t shift = 0;

      for (size_t i = 0; i < local_expansion_ratio; ++i) {
        for (size_t j = 0; j < pt_iter->coeff_count(); ++j) {
          if (shift == 0) {
            ct.data(poly_index)[coeff_mod_index * N + j] = (*pt_iter)[j];
          } else {
            ct.data(poly_index)[coeff_mod_index * N + j] +=
                ((*pt_iter)[j] << shift);
          }
        }

        ++pt_iter;
        shift += logt;
      }
    }
  }
}

void ComposeToCiphertext(seal::EncryptionParameters params,
                         const vector<seal::Plaintext> &pts,
                         seal::Ciphertext &ct) {
  return ComposeToCiphertext(params, pts.begin(),
                             pts.size() / ComputeExpansionRatio(params), ct);
}

uint64_t InvertMod(uint64_t m, const seal::Modulus &mod) {
  YACL_ENFORCE_LE(mod.uint64_count(), 1UL);
  uint64_t inverse = 0;
  YACL_ENFORCE(seal::util::try_invert_uint_mod(m, mod.value(), inverse));
  return inverse;
}

void VectorToPlaintext(const vector<uint64_t> &coeffs, seal::Plaintext &plain) {
  uint32_t coeff_count = coeffs.size();
  plain.resize(coeff_count);
  seal::util::set_uint(coeffs.data(), coeff_count, plain.data());
}

}  // namespace

/**********************************************************************************
SealPir
**********************************************************************************/
void SealPir::SetSealContext(size_t degree) {
  // degree >= 4096
  YACL_ENFORCE_GE(degree, (size_t)4096);
  enc_params_ = make_unique<seal::EncryptionParameters>(seal::scheme_type::bfv);
  enc_params_->set_poly_modulus_degree(degree);
  enc_params_->set_plain_modulus(
      seal::PlainModulus::Batching(degree, options_.logt + 1));
  enc_params_->set_coeff_modulus(seal::CoeffModulus::BFVDefault(degree));

  context_ = make_unique<seal::SEALContext>(*(enc_params_));
  // verify
  if (!context_->parameters_set()) {
    YACL_THROW("SEAL parameters not valid.");
  }
  if (!context_->using_keyswitching()) {
    YACL_THROW("SEAL parameters do not support key switching.");
  }
  if (!context_->first_context_data()->qualifiers().using_batching) {
    YACL_THROW("SEAL parameters do not support batching.");
  }

  encoder_ = make_unique<seal::BatchEncoder>(*context_);
  if (encoder_->slot_count() != enc_params_->poly_modulus_degree()) {
    YACL_THROW("Slot count not equal to poly modulus degree");
  }

  evaluator_ = std::make_unique<seal::Evaluator>(*context_);
}

void SealPir::SetPirParams(size_t ele_num, size_t ele_size, size_t d) {
  uint32_t N = enc_params_->poly_modulus_degree();
  seal::Modulus t = enc_params_->plain_modulus();
  uint32_t logt = floor(log2(t.value()));
  uint64_t elements_per_plaintext = ElementsPerPtxt(logt, N, ele_size);
  uint64_t num_of_plaintexts = PlaintextsPerDb(logt, N, ele_num, ele_size);

  vector<uint64_t> dimension_vec = GetDimensions(num_of_plaintexts, d);

  uint32_t expansion_ratio = 0;
  for (uint32_t i = 0; i < enc_params_->coeff_modulus().size(); ++i) {
    double logqi = log2(enc_params_->coeff_modulus()[i].value());
    expansion_ratio += ceil(logqi / logt);
  }

  pir_params_.dimension = d;
  pir_params_.ele_num = ele_num;
  pir_params_.ele_size = ele_size;
  pir_params_.elements_per_plaintext = elements_per_plaintext;
  pir_params_.num_of_plaintexts = num_of_plaintexts;
  pir_params_.expansion_ratio = expansion_ratio << 1;
  pir_params_.dimension_vec = dimension_vec;
  pir_params_.slot_cnt = N;
  pir_params_.enable_batching = true;
  pir_params_.enable_symmetric = false;
  pir_params_.enable_mswitching = true;
}

void SealPir::PrintPirParams() {
  const PirParams pir_params = pir_params_;
  uint32_t prod =
      accumulate(pir_params.dimension_vec.begin(),
                 pir_params.dimension_vec.end(), 1, multiplies<uint64_t>());

  SPDLOG_INFO("PIR Parameters");
  SPDLOG_INFO("number of elements: {}", pir_params.ele_num);
  SPDLOG_INFO("element size: {}", pir_params.ele_size);
  SPDLOG_INFO("elements per BFV plaintext: {}",
              pir_params.elements_per_plaintext);
  SPDLOG_INFO("dimensions for d-dimensional hyperrectangle: {}",
              pir_params.dimension);
  SPDLOG_INFO("number of BFV plaintexts (before padding): {}",
              pir_params.num_of_plaintexts);
  SPDLOG_INFO(
      "Number of BFV plaintexts after padding (to fill d-dimensional "
      "hyperrectangle): {}",
      prod);
  SPDLOG_INFO("expansion ratio: {}", pir_params.expansion_ratio);
  SPDLOG_INFO("Using symmetric encryption: {}", pir_params.enable_symmetric);
  SPDLOG_INFO("Using recursive mod switching: {}",
              pir_params.enable_mswitching);
  SPDLOG_INFO("slot count: {}", pir_params.slot_cnt);
  SPDLOG_INFO("==============================");
}

string SealPir::SerializePlaintexts(const vector<seal::Plaintext> &plains) {
  PlaintextsProto plains_proto;

  for (const auto &plain : plains) {
    string plain_bytes = SerializeSealObject<seal::Plaintext>(plain);

    plains_proto.add_data(plain_bytes.data(), plain_bytes.length());
  }
  return plains_proto.SerializeAsString();
}

vector<seal::Plaintext> SealPir::DeSerializePlaintexts(
    const string &plaintext_bytes, bool safe_load) {
  PlaintextsProto plains_proto;
  plains_proto.ParseFromArray(plaintext_bytes.data(), plaintext_bytes.length());

  vector<seal::Plaintext> plains(plains_proto.data_size());

  yacl::parallel_for(0, plains_proto.data_size(),
                     [&](int64_t begin, int64_t end) {
                       for (int i = begin; i < end; ++i) {
                         plains[i] = DeSerializeSealObject<seal::Plaintext>(
                             plains_proto.data(i), safe_load);
                       }
                     });
  return plains;
}

yacl::Buffer SealPir::SerializeCiphertexts(
    const vector<seal::Ciphertext> &ciphers) {
  CiphertextsProto ciphers_proto;

  for (const auto &cipher : ciphers) {
    string cipher_bytes = SerializeSealObject<seal::Ciphertext>(cipher);

    ciphers_proto.add_ciphers(cipher_bytes.data(), cipher_bytes.length());
  }

  yacl::Buffer b(ciphers_proto.ByteSizeLong());
  ciphers_proto.SerializePartialToArray(b.data(), b.size());
  return b;
}

vector<seal::Ciphertext> SealPir::DeSerializeCiphertexts(
    const CiphertextsProto &ciphers_proto, bool safe_load) {
  vector<seal::Ciphertext> ciphers(ciphers_proto.ciphers_size());

  yacl::parallel_for(0, ciphers_proto.ciphers_size(),
                     [&](int64_t begin, int64_t end) {
                       for (int i = begin; i < end; ++i) {
                         ciphers[i] = DeSerializeSealObject<seal::Ciphertext>(
                             ciphers_proto.ciphers(i), safe_load);
                       }
                     });
  return ciphers;
}

vector<seal::Ciphertext> SealPir::DeSerializeCiphertexts(
    const yacl::Buffer &ciphers_buffer, bool safe_load) {
  CiphertextsProto ciphers_proto;
  ciphers_proto.ParseFromArray(ciphers_buffer.data(), ciphers_buffer.size());

  return DeSerializeCiphertexts(ciphers_proto, safe_load);
}

yacl::Buffer SealPir::SerializeQuery(
    SealPirQueryProto *query_proto,
    const vector<vector<seal::Ciphertext>> &query_ciphers) {
  for (const auto &query_cipher : query_ciphers) {
    CiphertextsProto *ciphers_proto = query_proto->add_query_cipher();
    for (const auto &ciphertext : query_cipher) {
      string cipher_bytes = SerializeSealObject<seal::Ciphertext>(ciphertext);

      ciphers_proto->add_ciphers(cipher_bytes.data(), cipher_bytes.length());
    }
  }

  yacl::Buffer b(query_proto->ByteSizeLong());
  query_proto->SerializePartialToArray(b.data(), b.size());
  return b;
}

yacl::Buffer SealPir::SerializeQuery(
    const vector<vector<seal::Ciphertext>> &query_ciphers, size_t start_pos) {
  SealPirQueryProto query_proto;

  query_proto.set_ind_degree(0);
  query_proto.set_start_pos(start_pos);

  return SerializeQuery(&query_proto, query_ciphers);
}

vector<vector<seal::Ciphertext>> SealPir::DeSerializeQuery(
    const SealPirQueryProto &query_proto, bool safe_load) {
  vector<vector<seal::Ciphertext>> pir_query(query_proto.query_cipher_size());

  yacl::parallel_for(
      0, query_proto.query_cipher_size(), [&](int64_t begin, int64_t end) {
        for (int64_t i = begin; i < end; ++i) {
          const auto &ciphers = query_proto.query_cipher(i);

          pir_query[i].resize(ciphers.ciphers_size());
          for (int j = 0; j < ciphers.ciphers_size(); ++j) {
            pir_query[i][j] = DeSerializeSealObject<seal::Ciphertext>(
                ciphers.ciphers(j), safe_load);
          }
        }
      });
  return pir_query;
}

vector<vector<seal::Ciphertext>> SealPir::DeSerializeQuery(
    const yacl::Buffer &query_buffer, bool safe_load) {
  SealPirQueryProto query_proto;
  query_proto.ParseFromArray(query_buffer.data(), query_buffer.size());

  return DeSerializeQuery(query_proto, safe_load);
}

/**********************************************************************************
SealPirServer
**********************************************************************************/

SealPirServer::SealPirServer(const SealPirOptions &options,
                             shared_ptr<IDbPlaintextStore> plaintext_store)
    : SealPir(options),
      plaintext_store_(std::move(plaintext_store)),
      is_db_preprocessed_(false) {}

void SealPirServer::SetDatabase(const vector<yacl::ByteContainerView> &db_vec) {
  vector<uint8_t> db_flatten_bytes(db_vec.size() * options_.element_size);
  for (uint32_t i = 0; i < db_vec.size(); ++i) {
    YACL_ENFORCE_EQ(db_vec[i].size(), options_.element_size);
    memcpy(&db_flatten_bytes[i * options_.element_size], db_vec[i].data(),
           db_vec[i].size());
  }

  shared_ptr<IDbElementProvider> db_provider =
      make_shared<MemoryDbElementProvider>(std::move(db_flatten_bytes),
                                           options_.element_size);

  return SetDatabaseByProvider(db_provider);
}

void SealPirServer::SetDatabaseByProvider(
    const shared_ptr<IDbElementProvider> &db_provider) {
  uint64_t db_size = options_.element_number * pir_params_.ele_size;
  YACL_ENFORCE_EQ(db_provider->GetDbByteSize(), db_size);

  uint32_t N = enc_params_->poly_modulus_degree();
  uint32_t logt = floor(log2(enc_params_->plain_modulus().value()));

  uint64_t num_of_ptxt = pir_params_.num_of_plaintexts;
  uint64_t ele_per_ptxt = pir_params_.elements_per_plaintext;
  uint64_t bytes_per_ptxt = ele_per_ptxt * pir_params_.ele_size;
  uint64_t coeffs_per_ele = CoefficientsPerElement(logt, pir_params_.ele_size);
  uint64_t coeffs_per_ptxt = ele_per_ptxt * coeffs_per_ele;
  YACL_ENFORCE_LE(coeffs_per_ptxt, N);

  uint64_t db_num;
  if (options_.ind_degree == 0) {
    db_num = 1;
    num_of_ptxt =
        PlaintextsPerDb(logt, N, options_.element_number, pir_params_.ele_size);
  } else {
    db_num = ceil((double)options_.element_number / options_.ind_degree);
    num_of_ptxt =
        PlaintextsPerDb(logt, N, options_.ind_degree, pir_params_.ele_size);
  }

  uint64_t prod = 1;
  for (uint32_t i = 0; i < pir_params_.dimension; ++i) {
    prod *= pir_params_.dimension_vec[i];
  }
  YACL_ENFORCE_GT(prod, num_of_ptxt);

  plaintext_store_->SetSubDbNumber(db_num);

  for (uint64_t db_idx = 0; db_idx < db_num; ++db_idx) {
    vector<Plaintext> db_vec;
    db_vec.reserve(prod);

    uint32_t offset = db_idx * options_.ind_degree * pir_params_.ele_size;

    for (uint64_t i = 0; i < num_of_ptxt; ++i) {
      uint32_t process_bytes = 0;

      if (offset >= db_size) {
        break;
      } else if (offset + bytes_per_ptxt >= db_size) {
        process_bytes = db_size - offset;
      } else {
        process_bytes = bytes_per_ptxt;
      }
      YACL_ENFORCE_EQ(process_bytes % pir_params_.ele_size, 0UL);

      vector<uint8_t> bytes = db_provider->ReadElement(offset, process_bytes);
      uint64_t ele_in_chunk = process_bytes / pir_params_.ele_size;

      vector<uint64_t> coeffs(coeffs_per_ptxt);
      for (uint64_t ele = 0; ele < ele_in_chunk; ++ele) {
        vector<uint64_t> ele_coeffs = BytesToCoeffs(
            logt, gsl::span<uint8_t>(
                      bytes.data() + (pir_params_.ele_size * ele),
                      bytes.data() + (pir_params_.ele_size * (ele + 1))));
        copy(ele_coeffs.begin(), ele_coeffs.end(),
             coeffs.begin() + coeffs_per_ele * ele);
      }

      offset += process_bytes;
      uint64_t used = coeffs.size();
      YACL_ENFORCE_LE(used, coeffs_per_ptxt);

      // padding
      for (uint64_t j = 0; j < (N - used); ++j) {
        coeffs.push_back(1);
      }

      Plaintext plain;
      encoder_->encode(coeffs, plain);
      db_vec.push_back(std::move(plain));
    }

    uint64_t current_ptxts = db_vec.size();
    uint64_t matrix_ptxts = prod;
    YACL_ENFORCE_LE(current_ptxts, num_of_ptxt);

    vector<uint64_t> padding(N, 1);

    for (uint64_t i = 0; i < (matrix_ptxts - current_ptxts); ++i) {
      Plaintext plain;
      VectorToPlaintext(padding, plain);
      db_vec.push_back(plain);
    }

    yacl::parallel_for(0, db_vec.size(), [&](int64_t begin, int64_t end) {
      for (uint32_t i = begin; i < end; i++) {
        evaluator_->transform_to_ntt_inplace(db_vec[i],
                                             context_->first_parms_id());
      }
    });
    plaintext_store_->SavePlaintexts(db_vec, db_idx);
  }

  is_db_preprocessed_ = true;
}

yacl::Buffer SealPirServer::GenerateIndexReply(
    const yacl::Buffer &query_buffer) {
  SealPirQueryProto query_proto;
  query_proto.ParseFromArray(query_buffer.data(), query_buffer.size());

  PirQuery query = DeSerializeQuery(query_proto);
  yacl::Buffer reply_buffer =
      SerializeCiphertexts(GenerateReply(query, query_proto.start_pos(), 0));

  return reply_buffer;
}

SealPir::PirReply SealPirServer::GenerateReply(const SealPir::PirQuery &query,
                                               uint32_t start_pos,
                                               uint32_t client_id) {
  int N = enc_params_->poly_modulus_degree();
  uint32_t expansion_ratio = pir_params_.expansion_ratio;
  vector<uint64_t> dimension_vec = pir_params_.dimension_vec;

  uint32_t sub_db_idx = 0;
  if (options_.ind_degree > 0) {
    YACL_ENFORCE_EQ(start_pos % options_.ind_degree, 0UL);
    sub_db_idx = start_pos / options_.ind_degree;
  }
  vector<Plaintext> db_plaintext = plaintext_store_->ReadPlaintexts(sub_db_idx);

  vector<Plaintext> *cur = &db_plaintext;
  vector<Plaintext> intermediate_plain;

  uint64_t prod = 1;
  for (uint32_t i = 0; i < dimension_vec.size(); ++i) {
    prod *= dimension_vec[i];
  }

  for (uint32_t i = 0; i < dimension_vec.size(); ++i) {
    SPDLOG_INFO("Server: {}-th recursion level started ", i + 1);
    vector<Ciphertext> expanded_query;
    uint64_t ni = dimension_vec[i];

    for (uint32_t j = 0; j < query[i].size(); ++j) {
      uint64_t total = N;

      if (j == query[i].size() - 1) {
        uint64_t ni_mod_N = ni % N;
        // add the branch to handle the case that ni mod N == 0
        if (ni_mod_N != 0) {
          total = ni_mod_N;
        }
      }

      vector<Ciphertext> part_expanded_query =
          ExpandQuery(query[i][j], total, client_id);

      expanded_query.insert(expanded_query.end(),
                            make_move_iterator(part_expanded_query.begin()),
                            make_move_iterator(part_expanded_query.end()));
      part_expanded_query.clear();
    }
    YACL_ENFORCE_EQ(expanded_query.size(), ni);

    yacl::parallel_for(
        0, expanded_query.size(), [&](uint32_t begin, uint32_t end) {
          for (uint32_t jj = begin; jj < end; ++jj) {
            evaluator_->transform_to_ntt_inplace(expanded_query[jj]);
          }
        });

    if ((!is_db_preprocessed_) || i > 0) {
      yacl::parallel_for(0, cur->size(), [&](uint32_t begin, uint32_t end) {
        for (uint32_t jj = begin; jj < end; ++jj) {
          evaluator_->transform_to_ntt_inplace((*cur)[jj],
                                               context_->first_parms_id());
        }
      });
    }

    prod /= ni;

    vector<Ciphertext> intermediateCtxts(prod);

    yacl::parallel_for(0, prod, [&](int64_t begin, int64_t end) {
      for (int k = begin; k < end; ++k) {
        evaluator_->multiply_plain(expanded_query[0], (*cur)[k],
                                   intermediateCtxts[k]);

        Ciphertext tmp;
        for (uint64_t j = 1; j < ni; ++j) {
          evaluator_->multiply_plain(expanded_query[j], (*cur)[j * prod + k],
                                     tmp);
          evaluator_->add_inplace(intermediateCtxts[k], tmp);
        }
      }
    });

    yacl::parallel_for(
        0, intermediateCtxts.size(), [&](int64_t begin, int64_t end) {
          for (uint32_t jj = begin; jj < end; jj++) {
            evaluator_->transform_from_ntt_inplace(intermediateCtxts[jj]);
          }
        });

    if (i == dimension_vec.size() - 1) {
      return intermediateCtxts;
    } else {
      intermediate_plain.clear();
      intermediate_plain.reserve(expansion_ratio * prod);
      cur = &intermediate_plain;

      for (uint32_t j = 0; j < prod; ++j) {
        EncryptionParameters parms;
        if (pir_params_.enable_mswitching) {
          evaluator_->mod_switch_to_inplace(intermediateCtxts[j],
                                            context_->last_parms_id());
          parms = context_->last_context_data()->parms();
        } else {
          parms = context_->first_context_data()->parms();
        }

        vector<Plaintext> part_intermediate_plain =
            DecomposeToPlaintexts(parms, intermediateCtxts[j]);

        intermediate_plain.insert(
            intermediate_plain.end(),
            make_move_iterator(part_intermediate_plain.begin()),
            make_move_iterator(part_intermediate_plain.end()));
      }
      prod = intermediate_plain.size();
    }
  }
  YACL_ENFORCE(0);
  vector<Ciphertext> fail(1);
  return fail;
}

inline vector<Ciphertext> SealPirServer::ExpandQuery(
    const seal::Ciphertext &encrypted, uint64_t m, uint32_t client_id) {
  SPDLOG_INFO("expanding query......");
  int N = enc_params_->poly_modulus_degree();

  uint32_t logm = ceil(log2(m));
  YACL_ENFORCE_LE(logm, ceil(log2(N)));

  // handle the case that m = 1, logm = 0
  if (logm == 0) {
    vector<Ciphertext> result(1, encrypted);
    return result;
  }

  GaloisKeys &galkey = galoisKeys_[client_id];
  vector<int> galelts;
  for (int i = 0; i < ceil(log2(N)); ++i) {
    galelts.push_back((N + seal::util::exponentiate_uint(2, i)) /
                      seal::util::exponentiate_uint(2, i));
  }

  vector<Ciphertext> tmp;
  tmp.push_back(encrypted);
  Ciphertext tmpctxt_rotated;
  Ciphertext tmpctxt_shifted;
  Ciphertext tmpctxt_rotatedshifted;

  for (uint32_t i = 0; i < logm - 1; ++i) {
    vector<Ciphertext> new_tmp(tmp.size() << 1);
    int index_raw = (N << 1) - (1ULL << i);
    int index = (index_raw + N) % (N << 1);
    // int index = (index_raw * galelts[i]) % (N << 1);

    for (uint32_t j = 0; j < tmp.size(); ++j) {
      evaluator_->apply_galois(tmp[j], galelts[i], galkey, tmpctxt_rotated);
      evaluator_->add(tmp[j], tmpctxt_rotated, new_tmp[j]);

      MultiplyPowerOfX(tmp[j], tmpctxt_shifted, index_raw);
      MultiplyPowerOfX(tmpctxt_rotated, tmpctxt_rotatedshifted, index);
      evaluator_->add(tmpctxt_shifted, tmpctxt_rotatedshifted,
                      new_tmp[j + tmp.size()]);
    }

    tmp = new_tmp;
  }

  vector<Ciphertext> new_tmp(tmp.size() << 1);
  int index_raw = (N << 1) - (1ULL << (logm - 1));
  int index = (index_raw + N) % (N << 1);
  // int index = (index_raw * galelts[logm - 1]) % (N << 1);
  Plaintext two("2");

  for (uint32_t j = 0; j < tmp.size(); ++j) {
    if (j < (m - (1ULL << (logm - 1)))) {
      evaluator_->apply_galois(tmp[j], galelts[logm - 1], galkey,
                               tmpctxt_rotated);
      evaluator_->add(tmp[j], tmpctxt_rotated, new_tmp[j]);

      MultiplyPowerOfX(tmp[j], tmpctxt_shifted, index_raw);
      MultiplyPowerOfX(tmpctxt_rotated, tmpctxt_rotatedshifted, index);
      evaluator_->add(tmpctxt_shifted, tmpctxt_rotatedshifted,
                      new_tmp[j + tmp.size()]);
    } else {
      evaluator_->multiply_plain(tmp[j], two, new_tmp[j]);
    }
  }

  vector<Ciphertext> result(new_tmp.begin(), new_tmp.begin() + m);

  return result;
}

inline void SealPirServer::MultiplyPowerOfX(const Ciphertext &encrypted,
                                            Ciphertext &destination,
                                            uint32_t index) {
  int N = enc_params_->poly_modulus_degree();
  size_t coeff_mod_cnt = enc_params_->coeff_modulus().size() - 1;
  size_t encrypted_cnt = encrypted.size();

  destination = encrypted;

  for (size_t i = 0; i < encrypted_cnt; ++i) {
    for (size_t j = 0; j < coeff_mod_cnt; ++j) {
      seal::util::negacyclic_shift_poly_coeffmod(
          encrypted.data(i) + (j * N), N, index,
          enc_params_->coeff_modulus()[j], destination.data(i) + (j * N));
    }
  }
}

void SealPirServer::SetGaloisKey(uint32_t client_id, seal::GaloisKeys galkey) {
  galoisKeys_[client_id] = galkey;
}

string SealPirServer::SerializeDbPlaintext(int db_index) {
  return SerializePlaintexts(*db_vec_[db_index].get());
}

void SealPirServer::DeSerializeDbPlaintext(const string &db_serialize_bytes,
                                           int db_index) {
  vector<seal::Plaintext> plaintext_vec =
      DeSerializePlaintexts(db_serialize_bytes);

  db_vec_[db_index] = make_unique<vector<seal::Plaintext>>(plaintext_vec);
}

void SealPirServer::SetOneCt(Ciphertext one) {
  one_ = one;
  evaluator_->transform_to_ntt_inplace(one_);
}

/**********************************************************************************
SealPirClient
**********************************************************************************/
SealPirClient::SealPirClient(const SealPirOptions &options) : SealPir(options) {
  keygen_ = make_unique<seal::KeyGenerator>(*context_);

  SecretKey secret_key = keygen_->secret_key();
  PublicKey public_key;
  keygen_->create_public_key(public_key);

  encryptor_ = make_unique<Encryptor>(*context_, public_key);
  encryptor_->set_secret_key(secret_key);

  decryptor_ = make_unique<seal::Decryptor>(*context_, secret_key);
}

yacl::Buffer SealPirClient::GenerateIndexQuery(uint64_t ele_index,
                                               uint64_t &offset) {
  uint64_t query_index = ele_index;
  size_t start_pos = 0;
  if (options_.ind_degree > 0) {
    query_index = ele_index % options_.ind_degree;
    start_pos = ele_index - query_index;
  }

  uint64_t index = GetFVIndex(query_index);
  offset = GetFVOffset(query_index);
  yacl::Buffer query_buffer = SerializeQuery(GenerateQuery(index), start_pos);
  return query_buffer;
}

SealPir::PirQuery SealPirClient::GenerateQuery(uint64_t index) {
  indices_ = ComputeIndices(index, pir_params_.dimension_vec);

  SealPir::PirQuery result(pir_params_.dimension);
  int N = enc_params_->poly_modulus_degree();

  Plaintext pt(enc_params_->poly_modulus_degree());
  for (uint32_t i = 0; i < indices_.size(); ++i) {
    uint32_t num_ptxts = ceil(pir_params_.dimension_vec[i] / (double)N);

    for (uint32_t j = 0; j < num_ptxts; ++j) {
      pt.set_zero();

      if (indices_[i] >= j * N && indices_[i] < (j + 1) * N) {
        uint64_t real_index = indices_[i] - j * N;
        uint64_t ni = pir_params_.dimension_vec[i];

        uint64_t total = N;
        if (j == num_ptxts - 1) {
          uint64_t ni_mod_N = ni % N;
          if (ni_mod_N != 0) {
            total = ni_mod_N;
          }
        }
        uint64_t logm = ceil(log2(total));

        pt[real_index] = InvertMod(pow(2, logm), enc_params_->plain_modulus());
      }

      Ciphertext ct;
      if (pir_params_.enable_symmetric) {
        encryptor_->encrypt_symmetric(pt, ct);
      } else {
        encryptor_->encrypt(pt, ct);
      }

      result[i].push_back(ct);
    }
  }

  return result;
}

vector<uint8_t> SealPirClient::DecodeIndexReply(
    const yacl::Buffer &reply_buffer, uint64_t offset) {
  PirReply reply = DeSerializeCiphertexts(reply_buffer);
  vector<uint8_t> elems = DecodeReply(reply, offset);
  return elems;
}

vector<uint8_t> SealPirClient::DecodeReply(SealPir::PirReply &reply,
                                           uint64_t offset) {
  Plaintext ptxt = DecodeReply(reply);
  return ExtractBytes(ptxt, offset);
}

Plaintext SealPirClient::DecodeReply(SealPir::PirReply &reply) {
  EncryptionParameters parms;
  parms_id_type parms_id;
  if (pir_params_.enable_mswitching) {
    parms = context_->last_context_data()->parms();
    parms_id = context_->last_parms_id();
  } else {
    parms = context_->first_context_data()->parms();
    parms_id = context_->first_parms_id();
  }
  uint32_t expansion_ratio = ComputeExpansionRatio(parms);
  uint32_t d = pir_params_.dimension;

  vector<Ciphertext> tmp = reply;
  uint32_t ct_poly_count = tmp[0].size();
  for (uint32_t i = 0; i < d; ++i) {
    vector<Ciphertext> newtmp;
    vector<Plaintext> tmpplain;

    for (uint32_t j = 0; j < tmp.size(); ++j) {
      Plaintext ptxt;
      decryptor_->decrypt(tmp[j], ptxt);
      tmpplain.push_back(ptxt);

      if (j != 0 && (j + 1) % (expansion_ratio * ct_poly_count) == 0) {
        Ciphertext ctxt(*context_, parms_id);
        ComposeToCiphertext(parms, tmpplain, ctxt);

        newtmp.push_back(ctxt);

        tmpplain.clear();
      }
    }

    if (i == d - 1) {
      YACL_ENFORCE_EQ(tmp.size(), 1UL);
      return tmpplain[0];
    } else {
      tmpplain.clear();
      tmp = newtmp;
    }
  }

  YACL_ENFORCE(0);
  Plaintext fail;
  return fail;
}

seal::GaloisKeys SealPirClient::GenerateGaloisKeys() {
  vector<uint32_t> galois_elts;
  int N = enc_params_->poly_modulus_degree();
  int logN = seal::util::get_power_of_two(N);

  for (int i = 0; i < logN; ++i) {
    galois_elts.push_back((N + seal::util::exponentiate_uint(2, i)) /
                          seal::util::exponentiate_uint(2, i));
  }

  GaloisKeys gal_keys;
  keygen_->create_galois_keys(galois_elts, gal_keys);
  return gal_keys;
}

uint64_t SealPirClient::GetFVIndex(uint64_t element_index) {
  return static_cast<uint64_t>(element_index /
                               pir_params_.elements_per_plaintext);
}

uint64_t SealPirClient::GetFVOffset(uint64_t element_index) {
  return element_index % pir_params_.elements_per_plaintext;
}

Plaintext SealPirClient::Decrypt(Ciphertext ct) {
  Plaintext pt;
  decryptor_->decrypt(ct, pt);
  return pt;
}

vector<uint8_t> SealPirClient::ExtractBytes(seal::Plaintext pt,
                                            uint64_t offset) {
  uint32_t logt = floor(log2(enc_params_->plain_modulus().value()));
  uint64_t bytes_per_ptxt =
      pir_params_.elements_per_plaintext * pir_params_.ele_size;

  vector<uint8_t> elems(bytes_per_ptxt);
  vector<uint64_t> coeffs;
  encoder_->decode(pt, coeffs);
  CoeffsToBytes(logt, coeffs, elems, pir_params_.ele_size);

  return vector<uint8_t>(elems.begin() + offset * pir_params_.ele_size,
                         elems.begin() + (offset + 1) * pir_params_.ele_size);
}

Ciphertext SealPirClient::GetOne() {
  Plaintext pt("1");
  Ciphertext ct;
  if (pir_params_.enable_symmetric) {
    encryptor_->encrypt_symmetric(pt, ct);
  } else {
    encryptor_->encrypt(pt, ct);
  }
  return ct;
}

}  // namespace psi::sealpir