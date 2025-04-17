// Copyright 2025
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
#include <stdexcept>
#include <unordered_set>
#include <vector>

#include "apsi/oprf/oprf_common.h"
#include "gsl/span"
#include "seal/dynarray.h"
#include "seal/memorymanager.h"

namespace psi::dkpir {
// This is a copied class from "::apsi::oprf::OPRFReceiver", only method
// "process_items" has been modified. This class can correctly handle the
// shuffled oprf results from the server, but "::apsi::oprf::OPRFReceiver" can
// not.
class ShuffledOPRFReceiver {
 public:
  ShuffledOPRFReceiver(gsl::span<const ::apsi::Item> oprf_items)
      : oprf_queries_(pool_), inv_factor_data_(pool_) {
    process_items(oprf_items);
  }

  ShuffledOPRFReceiver(const ShuffledOPRFReceiver &) = delete;

  ShuffledOPRFReceiver(ShuffledOPRFReceiver &&) noexcept = default;

  ShuffledOPRFReceiver &operator=(const ShuffledOPRFReceiver &) = delete;

  ShuffledOPRFReceiver &operator=(ShuffledOPRFReceiver &&) noexcept = default;

  inline std::size_t item_count() const noexcept {
    return inv_factor_data_.item_count();
  }

  void process_responses(gsl::span<const unsigned char> oprf_responses,
                         gsl::span<::apsi::HashedItem> oprf_hashes,
                         gsl::span<::apsi::LabelKey> label_keys) const;

  void clear();

  std::vector<unsigned char> query_data() const;

 private:
  void set_item_count(std::size_t item_count);

  // All points use the same scalar
  void process_items(gsl::span<const ::apsi::Item> oprf_items);

  // For decrypting OPRF response
  class FactorData {
   public:
    static constexpr std::size_t factor_size =
        ::apsi::oprf::ECPoint::order_size;

    FactorData(seal::MemoryPoolHandle pool, std::size_t item_count = 0)
        : factor_data_(std::move(pool)) {
      resize(item_count);
    }

    ~FactorData() = default;

    FactorData(const FactorData &) = delete;

    FactorData(FactorData &&) noexcept = default;

    FactorData &operator=(const FactorData &) = delete;

    FactorData &operator=(FactorData &&) noexcept = default;

    std::size_t item_count() const noexcept { return item_count_; }

    auto get_factor(std::size_t index)
        -> ::apsi::oprf::ECPoint::scalar_span_type {
      if (index >= item_count_) {
        throw std::invalid_argument("index out of bounds");
      }
      return ::apsi::oprf::ECPoint::scalar_span_type(
          factor_data_.span().subspan(index * factor_size, factor_size).data(),
          factor_size);
    }

    auto get_factor(std::size_t index) const
        -> ::apsi::oprf::ECPoint::scalar_span_const_type {
      if (index >= item_count_) {
        throw std::invalid_argument("index out of bounds");
      }
      return ::apsi::oprf::ECPoint::scalar_span_const_type(
          factor_data_.span().subspan(index * factor_size, factor_size).data(),
          factor_size);
    }

   private:
    void resize(std::size_t item_count) {
      item_count_ = item_count;
      factor_data_.resize(item_count * factor_size);
    }

    seal::DynArray<unsigned char> factor_data_;

    std::size_t item_count_ = 0;
  };

  seal::MemoryPoolHandle pool_ =
      seal::MemoryManager::GetPool(seal::mm_prof_opt::mm_force_new, true);

  seal::DynArray<unsigned char> oprf_queries_;

  FactorData inv_factor_data_;
};  // class OPRFReceiver
}  // namespace psi::dkpir