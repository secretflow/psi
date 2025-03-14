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

#include "psi/algorithm/dkpir/oprf_receiver.h"

#include <array>

#include "apsi/oprf/oprf_receiver.h"
#include "apsi/util/utils.h"
#include "seal/util/defines.h"

namespace psi::dkpir {
void OPRFReceiver::set_item_count(std::size_t item_count) {
  auto new_pool =
      ::seal::MemoryManager::GetPool(::seal::mm_prof_opt::mm_force_new, true);
  oprf_queries_ = ::seal::DynArray<unsigned char>(
      item_count * ::apsi::oprf::oprf_query_size, new_pool);
  inv_factor_data_ = FactorData(new_pool, item_count);
  pool_ = std::move(new_pool);
}

void OPRFReceiver::clear() { set_item_count(0); }

std::vector<unsigned char> OPRFReceiver::query_data() const {
  return {oprf_queries_.cbegin(), oprf_queries_.cend()};
}

void OPRFReceiver::process_items(gsl::span<const ::apsi::Item> oprf_items) {
  set_item_count(oprf_items.size());

  // Create a random scalar for OPRF and save its inverse
  // Note: All points use the same scalar, otherwise shuffle will lead to
  // incorrect results
  ::apsi::oprf::ECPoint::scalar_type random_scalar;
  ::apsi::oprf::ECPoint::MakeRandomNonzeroScalar(random_scalar);

  auto oprf_out_ptr = oprf_queries_.begin();
  for (size_t i = 0; i < item_count(); i++) {
    // Create an elliptic curve point from the item
    ::apsi::oprf::ECPoint ecpt(oprf_items[i].get_as<const unsigned char>());

    ::apsi::oprf::ECPoint::InvertScalar(random_scalar,
                                        inv_factor_data_.get_factor(i));

    // Multiply our point with the random scalar
    ecpt.scalar_multiply(random_scalar, false);

    // Save the result to items_buffer
    ecpt.save(::apsi::oprf::ECPoint::point_save_span_type{
        oprf_out_ptr, ::apsi::oprf::oprf_query_size});

    // Move forward
    std::advance(oprf_out_ptr, ::apsi::oprf::oprf_query_size);
  }
}

void OPRFReceiver::process_responses(
    gsl::span<const unsigned char> oprf_responses,
    gsl::span<::apsi::HashedItem> oprf_hashes,
    gsl::span<::apsi::LabelKey> label_keys) const {
  if (oprf_hashes.size() != item_count()) {
    throw std::invalid_argument("oprf_hashes has invalid size");
  }
  if (label_keys.size() != item_count()) {
    throw std::invalid_argument("label_keys has invalid size");
  }
  if (oprf_responses.size() !=
      item_count() * ::apsi::oprf::oprf_response_size) {
    throw std::invalid_argument(
        "oprf_responses size is incompatible with oprf_hashes size");
  }

  auto oprf_in_ptr = oprf_responses.data();
  for (size_t i = 0; i < item_count(); i++) {
    // Load the point from items_buffer
    ::apsi::oprf::ECPoint ecpt;
    ecpt.load(::apsi::oprf::ECPoint::point_save_span_const_type{
        oprf_in_ptr, ::apsi::oprf::oprf_response_size});

    // Multiply with inverse random scalar
    ecpt.scalar_multiply(inv_factor_data_.get_factor(i), false);

    // Extract the item hash and the label encryption key
    std::array<unsigned char, ::apsi::oprf::ECPoint::hash_size>
        item_hash_and_label_key;
    ecpt.extract_hash(item_hash_and_label_key);

    // The first 16 bytes represent the item hash; the next 32 bytes represent
    // the label encryption key
    ::apsi::util::copy_bytes(item_hash_and_label_key.data(),
                             ::apsi::oprf::oprf_hash_size,
                             oprf_hashes[i].value().data());
    ::apsi::util::copy_bytes(
        item_hash_and_label_key.data() + ::apsi::oprf::oprf_hash_size,
        ::apsi::label_key_byte_count, label_keys[i].data());

    // Move forward
    std::advance(oprf_in_ptr, ::apsi::oprf::oprf_response_size);
  }
}
}  // namespace psi::dkpir