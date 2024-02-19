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

#include "psi/rr22/okvs/baxos.h"

#include <algorithm>
#include <array>
#include <future>
#include <unordered_set>
#include <vector>

#include "spdlog/spdlog.h"

#include "psi/rr22/okvs/simple_index.h"

namespace psi::rr22::okvs {

uint64_t Baxos::GetBinSize(uint64_t num_bins, uint64_t num_balls,
                           uint64_t stat_sec_param) {
  return SimpleIndex::GetBinSize(num_bins, num_balls, stat_sec_param);
}

template void Baxos::ImplParSolve<uint8_t>(
    absl::Span<const uint128_t> inputs_, const PxVector& vals_, PxVector& p_,
    const std::shared_ptr<yacl::crypto::Prg<uint8_t>>& prng,
    uint64_t num_threads, PxVector::Helper& h);

template void Baxos::ImplParSolve<uint16_t>(
    absl::Span<const uint128_t> inputs_, const PxVector& vals_, PxVector& p_,
    const std::shared_ptr<yacl::crypto::Prg<uint8_t>>& prng,
    uint64_t num_threads, PxVector::Helper& h);

template void Baxos::ImplParSolve<uint32_t>(
    absl::Span<const uint128_t> inputs_, const PxVector& vals_, PxVector& p_,
    const std::shared_ptr<yacl::crypto::Prg<uint8_t>>& prng,
    uint64_t num_threads, PxVector::Helper& h);

template void Baxos::ImplParSolve<uint64_t>(
    absl::Span<const uint128_t> inputs_, const PxVector& vals_, PxVector& p_,
    const std::shared_ptr<yacl::crypto::Prg<uint8_t>>& prng,
    uint64_t num_threads, PxVector::Helper& h);

template void Baxos::ImplParDecode<uint8_t>(absl::Span<const uint128_t> inputs,
                                            PxVector& values,
                                            const PxVector& pp,
                                            PxVector::Helper& h,
                                            uint64_t num_threads);

template void Baxos::ImplParDecode<uint16_t>(absl::Span<const uint128_t> inputs,
                                             PxVector& values,
                                             const PxVector& pp,
                                             PxVector::Helper& h,
                                             uint64_t num_threads);

template void Baxos::ImplParDecode<uint32_t>(absl::Span<const uint128_t> inputs,
                                             PxVector& values,
                                             const PxVector& pp,
                                             PxVector::Helper& h,
                                             uint64_t num_threads);

template void Baxos::ImplParDecode<uint64_t>(absl::Span<const uint128_t> inputs,
                                             PxVector& values,
                                             const PxVector& pp,
                                             PxVector::Helper& h,
                                             uint64_t num_threads);

template void Baxos::ImplDecodeBatch<uint8_t>(
    absl::Span<const uint128_t> inputs, PxVector& values, const PxVector& pp,
    PxVector::Helper& h);
template void Baxos::ImplDecodeBatch<uint16_t>(
    absl::Span<const uint128_t> inputs, PxVector& values, const PxVector& pp,
    PxVector::Helper& h);
template void Baxos::ImplDecodeBatch<uint32_t>(
    absl::Span<const uint128_t> inputs, PxVector& values, const PxVector& pp,
    PxVector::Helper& h);
template void Baxos::ImplDecodeBatch<uint64_t>(
    absl::Span<const uint128_t> inputs, PxVector& values, const PxVector& pp,
    PxVector::Helper& h);

template void Baxos::ImplDecodeBin(uint64_t bin_idx,
                                   absl::Span<uint128_t> hashes,
                                   PxVector& values, PxVector& values_buff,
                                   absl::Span<uint64_t> in_idxs,
                                   const PxVector& PP, PxVector::Helper& h,
                                   Paxos<uint8_t>& paxos);

template void Baxos::ImplDecodeBin(uint64_t bin_idx,
                                   absl::Span<uint128_t> hashes,
                                   PxVector& values, PxVector& values_buff,
                                   absl::Span<uint64_t> in_idxs,
                                   const PxVector& PP, PxVector::Helper& h,
                                   Paxos<uint16_t>& paxos);

template void Baxos::ImplDecodeBin(uint64_t bin_idx,
                                   absl::Span<uint128_t> hashes,
                                   PxVector& values, PxVector& values_buff,
                                   absl::Span<uint64_t> in_idxs,
                                   const PxVector& PP, PxVector::Helper& h,
                                   Paxos<uint32_t>& paxos);

template void Baxos::ImplDecodeBin(uint64_t bin_idx,
                                   absl::Span<uint128_t> hashes,
                                   PxVector& values, PxVector& values_buff,
                                   absl::Span<uint64_t> in_idxs,
                                   const PxVector& PP, PxVector::Helper& h,
                                   Paxos<uint64_t>& paxos);

void Baxos::Solve(absl::Span<const uint128_t> inputs,
                  const absl::Span<uint128_t> values,
                  absl::Span<uint128_t> output,
                  const std::shared_ptr<yacl::crypto::Prg<uint8_t>>& prng,
                  uint64_t num_threads) {
  PxVector V(values);
  PxVector P(output);
  auto h = P.DefaultHelper();
  Solve(inputs, V, P, prng, num_threads, h);
}

void Baxos::Solve(absl::Span<const uint128_t> inputs, const PxVector& V,
                  PxVector& P,
                  const std::shared_ptr<yacl::crypto::Prg<uint8_t>>& prng,
                  uint64_t num_threads, PxVector::Helper& h) {
  // select the smallest index type which will work.
  auto bit_length =
      RoundUpTo(yacl::math::Log2Ceil((paxos_param_.sparse_size + 1)), 8);

  SPDLOG_DEBUG("bit_length:{}", bit_length);

  if (bit_length <= 8) {
    ImplParSolve<uint8_t>(inputs, V, P, prng, num_threads, h);
  } else if (bit_length <= 16) {
    ImplParSolve<uint16_t>(inputs, V, P, prng, num_threads, h);
  } else if (bit_length <= 32) {
    ImplParSolve<uint32_t>(inputs, V, P, prng, num_threads, h);
  } else {
    ImplParSolve<uint64_t>(inputs, V, P, prng, num_threads, h);
  }
}

template <typename IdxType>
void Baxos::ImplParSolve(
    absl::Span<const uint128_t> inputs_param, const PxVector& vals_,
    PxVector& p_, const std::shared_ptr<yacl::crypto::Prg<uint8_t>>& prng,
    uint64_t num_threads, PxVector::Helper& h) {
  YACL_ENFORCE(p_.size() == size(), "p size:{} baox size:{}", p_.size(),
               size());
  YACL_ENFORCE(inputs_param.size() <= num_items_, "input size:{}, num_item:{}",
               inputs_param.size());

  SPDLOG_DEBUG(
      "item size:{} paxos size:{} num_bins_:{}, items_per_bin_:{} idxType:{}",
      inputs_param.size(), size(), num_bins_, items_per_bin_, sizeof(IdxType));

  if (num_bins_ == 1) {
    Paxos<IdxType> paxos;
    paxos.Init(num_items_, paxos_param_, seed_);

    paxos.SetInput(inputs_param);

    paxos.Encode(vals_, p_, h, prng);

    return;
  }

  num_threads = std::max<uint64_t>(1, num_threads);

  SPDLOG_DEBUG("num_threads: {}", num_threads);

  static constexpr const uint64_t batch_size = 32;

  // total number of bins between all threads.
  auto total_num_bins = num_bins_ * num_threads;

  auto items_per_thrd = (num_items_ + num_threads - 1) / num_threads;

  SPDLOG_DEBUG("num_bins_:{} total_num_bins: {}, items_per_thrd:{}", num_bins_,
               total_num_bins, items_per_thrd);

  // maximum number of items any single thread will map to a bin.
  auto per_thrd_max_bin_size = GetBinSize(num_bins_, items_per_thrd, ssp_);
  SPDLOG_DEBUG("per_thrd_max_bin_size:{}", per_thrd_max_bin_size);

  // the combined max size of the i'th bin held by each thread.
  uint64_t combined_max_bin_size = per_thrd_max_bin_size * num_threads;
  SPDLOG_DEBUG("combined_max_bin_size: {}", combined_max_bin_size);

  // keeps track of the size of each bin for each thread. Additional spaces to
  // prevent false sharing
  std::vector<uint64_t> thrd_bin_sizes_data(num_threads * num_bins_);
  MatrixView<uint64_t> thrd_bin_sizes_mtx(thrd_bin_sizes_data.data(),
                                          num_threads, num_bins_);

  SPDLOG_DEBUG("thrd_bin_sizes_mtx row: {}, col: {}", thrd_bin_sizes_mtx.rows(),
               thrd_bin_sizes_mtx.cols());

  // keeps track of input index of each item in each bin,thread.
  std::vector<uint64_t> input_mapping(total_num_bins * per_thrd_max_bin_size);

  // for the given thread, bin, return the list which map the bin
  // value back to the input value.
  auto GetInputMapping = [&](uint64_t thrd_idx, uint64_t bin_idx) {
    auto bin_begin = combined_max_bin_size * bin_idx;
    auto thrd_begin = per_thrd_max_bin_size * thrd_idx;
    absl::Span<uint64_t> mapping(input_mapping.data() + bin_begin + thrd_begin,
                                 per_thrd_max_bin_size);
    YACL_ENFORCE(
        (input_mapping.data() + total_num_bins * per_thrd_max_bin_size) >=
        (mapping.data() + mapping.size()));
    return mapping;
  };

  auto val_backing = h.NewVec(total_num_bins * per_thrd_max_bin_size);

  // get the values mapped to the given bin by the given thread.
  auto GetValues = [&](uint64_t thrd_idx, uint64_t bin_idx) {
    auto bin_begin = combined_max_bin_size * bin_idx;
    auto thrd_begin = per_thrd_max_bin_size * thrd_idx;

    return val_backing.subspan(bin_begin + thrd_begin, per_thrd_max_bin_size);
  };

  std::vector<uint128_t> hash_backing(total_num_bins * per_thrd_max_bin_size);

  // get the hashes mapped to the given bin by the given thread.
  auto GetHashes = [&](uint64_t thrd_idx, uint64_t bin_idx) {
    auto bin_begin = combined_max_bin_size * bin_idx;
    auto thrd_begin = per_thrd_max_bin_size * thrd_idx;

    return absl::Span<uint128_t>(hash_backing.data() + bin_begin + thrd_begin,
                                 per_thrd_max_bin_size);
  };

  libdivide::libdivide_u64_t divider = libdivide::libdivide_u64_gen(num_bins_);

  AesCrHash aes_crhasher(seed_);

  std::atomic<uint64_t> num_done(0);
  std::promise<void> hashing_done_prom;
  auto hashing_done_fu = hashing_done_prom.get_future().share();

  auto routine = [&](uint64_t thrd_idx) {
    auto begin = (inputs_param.size() * thrd_idx) / num_threads;
    auto end = (inputs_param.size() * (thrd_idx + 1)) / num_threads;
    auto inputs_span = inputs_param.subspan(begin, end - begin);

    SPDLOG_DEBUG("inputs size:{}, thrd_idx:{}", inputs_span.size(), thrd_idx);

    // hash all the inputs in my range [begin, end) into their bin.
    // Each thread will have its own set of bins. ie a total of numThreads *
    // numBins bins in total. These bin will need to be merged. Each thread will
    // also get its own reverse mapping.
    {
      SPDLOG_DEBUG("thrd_idx:{}", thrd_idx);
      auto bin_sizes = thrd_bin_sizes_mtx[thrd_idx];

      SPDLOG_DEBUG("bin_sizes:{}", bin_sizes.size());

      auto in_iter = inputs_span.data();
      std::array<uint128_t, batch_size> hashes;

      auto main = inputs_span.size() / batch_size * batch_size;
      std::array<uint64_t, batch_size> bin_idxs;

      uint64_t i = 0;
      auto in_idx = begin;
      for (; i < main; i += batch_size, in_iter += batch_size) {
        aes_crhasher.Hash(
            absl::MakeSpan((uint8_t*)(in_iter + 0), sizeof(uint128_t) * 8),
            absl::MakeSpan((uint8_t*)(hashes.data() + 0),
                           sizeof(uint128_t) * 8));
        aes_crhasher.Hash(
            absl::MakeSpan((uint8_t*)(in_iter + 8), sizeof(uint128_t) * 8),
            absl::MakeSpan((uint8_t*)(hashes.data() + 8),
                           sizeof(uint128_t) * 8));
        aes_crhasher.Hash(
            absl::MakeSpan((uint8_t*)(in_iter + 16), sizeof(uint128_t) * 8),
            absl::MakeSpan((uint8_t*)(hashes.data() + 16),
                           sizeof(uint128_t) * 8));
        aes_crhasher.Hash(
            absl::MakeSpan((uint8_t*)(in_iter + 24), sizeof(uint128_t) * 8),
            absl::MakeSpan((uint8_t*)(hashes.data() + 24),
                           sizeof(uint128_t) * 8));

        for (uint64_t k = 0; k < batch_size; ++k) {
          bin_idxs[k] = BinIdxCompress(hashes[k]);
          SPDLOG_DEBUG("k:{}, bin_idxs:{} ", k, bin_idxs[k]);
        }

        DoMod32(bin_idxs.data(), &divider, num_bins_);

        for (uint64_t k = 0; k < batch_size; ++k, ++in_idx) {
          auto bin_idx = bin_idxs[k];
          SPDLOG_DEBUG("k:{} bin_idx:{}", k, bin_idx);
          auto bs = bin_sizes[bin_idx]++;

          SPDLOG_DEBUG("i:{}, main:{} batch_size:{}", i, main, batch_size);
          SPDLOG_DEBUG("k:{} thrd_idx: {}, bin_idx: {},bs: {}, in_idx: {}", k,
                       thrd_idx, bin_idx, bs, in_idx);

          SPDLOG_DEBUG("total_num_bins:{} per_thrd_max_bin_size:{}",
                       total_num_bins, per_thrd_max_bin_size);

          GetInputMapping(thrd_idx, bin_idx)[bs] = in_idx;
          h.Assign(GetValues(thrd_idx, bin_idx)[bs], vals_[in_idx]);
          GetHashes(thrd_idx, bin_idx)[bs] = hashes[k];
        }
      }

      for (uint64_t k = 0; i < inputs_span.size();
           ++i, ++in_iter, ++k, ++in_idx) {
        hashes[k] = aes_crhasher.Hash(*in_iter);

        auto bin_idx = ModNumBins(hashes[k]);
        auto bs = bin_sizes[bin_idx]++;
        YACL_ENFORCE(bs < per_thrd_max_bin_size);

        GetInputMapping(thrd_idx, bin_idx)[bs] = in_idx;
        h.Assign(GetValues(thrd_idx, bin_idx)[bs], vals_[in_idx]);
        GetHashes(thrd_idx, bin_idx)[bs] = hashes[k];
      }
    }

    auto paxos_size_per = paxos_param_.size();

    std::vector<IdxType> rows_data(items_per_bin_ * weight_);
    std::vector<IdxType> col_backing_data(items_per_bin_ * weight_);

    std::vector<IdxType> col_weights_data(paxos_param_.sparse_size);

    std::vector<absl::Span<IdxType>> cols_data(paxos_param_.sparse_size);

    // block until all threads have mapped all items.
    if (++num_done == num_threads) {
      hashing_done_prom.set_value();
    } else {
      hashing_done_fu.get();
    }

    Paxos<IdxType> paxos;

    // this thread will iterator over its assigned bins. This thread
    // will aggregate all the items mapped to the ith bin (which are currently
    // stored in a per thread local).
    for (uint64_t bin_idx = thrd_idx; bin_idx < num_bins_;
         bin_idx += num_threads) {
      // get the actual bin size.
      uint64_t bin_size = 0;
      for (uint64_t i = 0; i < num_threads; ++i) {
        bin_size += thrd_bin_sizes_mtx(i, bin_idx);
      }

      YACL_ENFORCE(bin_size <= items_per_bin_);

      paxos.Init(bin_size, paxos_param_, seed_);

      SPDLOG_DEBUG("bin_size:{} weight:{} sparse:{}", bin_size, weight_,
                   paxos_param_.sparse_size);

      MatrixView<IdxType> rows_mtx(rows_data.data(), bin_size, weight_);
      absl::Span<IdxType> col_backing =
          absl::MakeSpan(col_backing_data.data(), bin_size * weight_);
      absl::Span<IdxType> col_weights =
          absl::MakeSpan(col_weights_data.data(), paxos_param_.sparse_size);
      absl::Span<absl::Span<IdxType>> cols = absl::MakeSpan(cols_data);

      auto bin_begin = combined_max_bin_size * bin_idx;
      auto values = val_backing.subspan(bin_begin, bin_size);
      auto hashes =
          absl::Span<uint128_t>(hash_backing.data() + bin_begin, bin_size);
      auto output = p_.subspan(paxos_size_per * bin_idx, paxos_size_per);

      // for each thread, copy the hashes,values that it mapped
      // to this bin.
      uint64_t bin_pos = thrd_bin_sizes_mtx(0, bin_idx);
      YACL_ENFORCE(bin_pos <= per_thrd_max_bin_size);
      YACL_ENFORCE(hashes.data() == GetHashes(0, bin_idx).data());

      for (uint64_t i = 1; i < num_threads; ++i) {
        auto size = thrd_bin_sizes_mtx(i, bin_idx);
        YACL_ENFORCE(size <= per_thrd_max_bin_size);

        auto thrd_hashes = GetHashes(i, bin_idx);
        auto thrd_vals = GetValues(i, bin_idx);

        std::memmove(hashes.data() + bin_pos, thrd_hashes.data(),
                     size * sizeof(uint128_t));

        for (uint64_t j = 0; j < size; ++j) {
          h.Assign(values[bin_pos + j], thrd_vals[j]);
        }

        bin_pos += size;
      }

      // compute the rows and count the column weight.
      std::memset(col_weights.data(), 0, col_weights.size() * sizeof(IdxType));
      auto rIter = rows_mtx.data();
      if (weight_ == 3) {
        auto main = bin_size / batch_size * batch_size;

        uint64_t i = 0;
        for (; i < main; i += batch_size) {
          paxos.hasher_.BuildRow32(absl::MakeSpan(&hashes[i], batch_size),
                                   absl::MakeSpan(rIter, weight_ * batch_size));
          for (uint64_t j = 0; j < batch_size; ++j) {
            ++col_weights[rIter[0]];
            ++col_weights[rIter[1]];
            ++col_weights[rIter[2]];
            rIter += weight_;
          }
        }
        for (; i < bin_size; ++i) {
          paxos.hasher_.BuildRow(hashes[i], absl::MakeSpan(rIter, weight_));

          ++col_weights[rIter[0]];
          ++col_weights[rIter[1]];
          ++col_weights[rIter[2]];
          rIter += weight_;
        }
      } else {
        for (uint64_t i = 0; i < bin_size; ++i) {
          paxos.hasher_.BuildRow(hashes[i], absl::MakeSpan(rIter, weight_));
          for (uint64_t k = 0; k < weight_; ++k) {
            ++col_weights[rIter[k]];
          }
          rIter += weight_;
        }
      }

      paxos.SetInput(rows_mtx, hashes, cols, col_backing, col_weights);

      paxos.Encode(values, output, h, prng);
    }
  };

  std::vector<std::thread> thrds(num_threads - 1);

  for (uint64_t i = 0; i < thrds.size(); ++i) {
    thrds[i] = std::thread(routine, i);
  }

  routine(thrds.size());

  for (uint64_t i = 0; i < thrds.size(); ++i) {
    thrds[i].join();
  }
}

void Baxos::Decode(absl::Span<const uint128_t> inputs,
                   absl::Span<uint128_t> values, const absl::Span<uint128_t> p,
                   uint64_t num_threads) {
  PxVector V(values);
  PxVector P(p);
  auto h = V.DefaultHelper();

  Decode(inputs, V, P, h, num_threads);
}

void Baxos::Decode(absl::Span<const uint128_t> inputs, PxVector& V,
                   const PxVector& P, PxVector::Helper& h,
                   uint64_t num_threads) {
  auto bit_length =
      RoundUpTo(yacl::math::Log2Ceil(paxos_param_.sparse_size + 1), 8);

  if (bit_length <= 8) {
    ImplParDecode<uint8_t>(inputs, V, P, h, num_threads);
  } else if (bit_length <= 16) {
    ImplParDecode<uint16_t>(inputs, V, P, h, num_threads);
  } else if (bit_length <= 32) {
    ImplParDecode<uint32_t>(inputs, V, P, h, num_threads);
  } else {
    ImplParDecode<uint64_t>(inputs, V, P, h, num_threads);
  }
}

template <typename IdxType>
void Baxos::ImplDecodeBin([[maybe_unused]] uint64_t bin_idx,
                          absl::Span<uint128_t> hashes, PxVector& values,
                          PxVector& values_buff, absl::Span<uint64_t> in_idxs,
                          const PxVector& PP, PxVector::Helper& h,
                          Paxos<IdxType>& paxos) {
  constexpr uint64_t batch_size = 32;
  constexpr uint64_t max_weight_size = 20;

  auto batch_count = (hashes.size() / batch_size) * batch_size;

  YACL_ENFORCE(weight_ <= max_weight_size);

  yacl::Buffer _backing_buffer(sizeof(IdxType) * max_weight_size * batch_size);
  absl::Span<IdxType> _backing = absl::MakeSpan(
      (IdxType*)(_backing_buffer.data()), max_weight_size * batch_size);

  MatrixView<IdxType> row(_backing.data(), batch_size, weight_);

  YACL_ENFORCE(values_buff.size() >= batch_size);

  uint64_t i = 0;

  for (; i < batch_count; i += batch_size) {
    paxos.hasher_.BuildRow32(absl::MakeSpan(&hashes[i], 32),
                             absl::MakeSpan(row.data(), batch_size * weight_));
    paxos.Decode32(absl::MakeSpan(row.data(), batch_size * weight_),
                   absl::MakeSpan(&hashes[i], batch_size),
                   absl::MakeSpan(values_buff[0], batch_size), PP, h);

    if (add_to_decode_) {
      for (uint64_t k = 0; k < batch_size; ++k) {
        h.Add(values[in_idxs[i + k]], values_buff[k]);
      }
    } else {
      for (uint64_t k = 0; k < batch_size; ++k) {
        h.Assign(values[in_idxs[i + k]], values_buff[k]);
      }
    }
  }

  for (; i < hashes.size(); ++i) {
    paxos.hasher_.BuildRow(hashes[i], absl::MakeSpan(row.data(), weight_));

    auto v = values[in_idxs[i]];

    if (add_to_decode_) {
      paxos.Decode1(absl::MakeSpan(row.data(), weight_), hashes[i],
                    values_buff[0], PP, h);
      h.Add(v, values_buff[0]);
    } else {
      paxos.Decode1(absl::MakeSpan(row.data(), weight_), hashes[i], v, PP, h);
    }
  }
}

template <typename IdxType>
void Baxos::ImplDecodeBatch(absl::Span<const uint128_t> inputs,
                            PxVector& values, const PxVector& pp,
                            PxVector::Helper& h) {
  uint64_t decode_size = std::min<uint64_t>(512, inputs.size());

  yacl::Buffer batches_data_buffer(num_bins_ * decode_size * sizeof(uint128_t));
  absl::Span<uint128_t> batches_data = absl::MakeSpan(
      (uint128_t*)(batches_data_buffer.data()), num_bins_ * decode_size);
  MatrixView<uint128_t> batches_mtx(batches_data.data(), num_bins_,
                                    decode_size);

  yacl::Buffer in_idxs_data_buffer(num_bins_ * decode_size * sizeof(uint64_t));
  absl::Span<uint64_t> in_idxs_data = absl::MakeSpan(
      (uint64_t*)(in_idxs_data_buffer.data()), num_bins_ * decode_size);
  MatrixView<uint64_t> in_idxs_mtx(in_idxs_data.data(), num_bins_, decode_size);

  std::vector<uint64_t> batch_sizes(num_bins_);

  AesCrHash aes_crhasher(reinterpret_cast<uint128_t>(seed_));

  auto in_iter = inputs.data();
  Paxos<IdxType> paxos;
  auto size_per = size() / num_bins_;
  paxos.Init(1, paxos_param_, seed_);
  auto buff = h.NewVec(32);

  static const uint32_t batch_size = 32;
  auto main = inputs.size() / batch_size * batch_size;
  std::array<uint128_t, batch_size> buffer;
  std::array<uint64_t, batch_size> bin_idxs;

  uint64_t i = 0;
  libdivide::libdivide_u64_t divider = libdivide::libdivide_u64_gen(num_bins_);

  // iterate over the input
  for (; i < main; i += batch_size, in_iter += batch_size) {
    aes_crhasher.Hash(absl::MakeSpan(in_iter, batch_size),
                      absl::MakeSpan(buffer));

    for (uint64_t j = 0; j < batch_size; j += 8) {
      bin_idxs[j + 0] = BinIdxCompress(buffer[j + 0]);
      bin_idxs[j + 1] = BinIdxCompress(buffer[j + 1]);
      bin_idxs[j + 2] = BinIdxCompress(buffer[j + 2]);
      bin_idxs[j + 3] = BinIdxCompress(buffer[j + 3]);
      bin_idxs[j + 4] = BinIdxCompress(buffer[j + 4]);
      bin_idxs[j + 5] = BinIdxCompress(buffer[j + 5]);
      bin_idxs[j + 6] = BinIdxCompress(buffer[j + 6]);
      bin_idxs[j + 7] = BinIdxCompress(buffer[j + 7]);
    }

    DoMod32(bin_idxs.data(), &divider, num_bins_);

    for (uint64_t k = 0; k < batch_size; ++k) {
      auto bin_idx = bin_idxs[k];

      batches_mtx(bin_idx, batch_sizes[bin_idx]) = buffer[k];
      in_idxs_mtx(bin_idx, batch_sizes[bin_idx]) = i + k;
      ++batch_sizes[bin_idx];

      if (batch_sizes[bin_idx] == decode_size) {
        auto p = pp.subspan(bin_idx * size_per, size_per);
        auto idxs = in_idxs_mtx[bin_idx];
        ImplDecodeBin<IdxType>(bin_idx, batches_mtx[bin_idx], values, buff,
                               idxs, p, h, paxos);

        batch_sizes[bin_idx] = 0;
      }
    }
  }

  for (; i < inputs.size(); ++i, ++in_iter) {
    auto k = 0;

    aes_crhasher.Hash(absl::MakeSpan(in_iter, 1),
                      absl::MakeSpan(&(buffer[k]), 1));

    auto bin_idx = ModNumBins(buffer[k]);

    batches_mtx(bin_idx, batch_sizes[bin_idx]) = buffer[k];
    in_idxs_mtx(bin_idx, batch_sizes[bin_idx]) = i + k;
    ++batch_sizes[bin_idx];

    if (batch_sizes[bin_idx] == decode_size) {
      auto p = pp.subspan(bin_idx * size_per, size_per);
      ImplDecodeBin<IdxType>(bin_idx, batches_mtx[bin_idx], values, buff,
                             in_idxs_mtx[bin_idx], p, h, paxos);

      batch_sizes[bin_idx] = 0;
    }
  }

  for (uint64_t bin_idx = 0; bin_idx < num_bins_; ++bin_idx) {
    if (batch_sizes[bin_idx]) {
      auto p = pp.subspan(bin_idx * size_per, size_per);
      auto b = batches_mtx[bin_idx].subspan(0, batch_sizes[bin_idx]);
      ImplDecodeBin<IdxType>(bin_idx, b, values, buff, in_idxs_mtx[bin_idx], p,
                             h, paxos);
    }
  }
}

template <typename IdxType>
void Baxos::ImplParDecode(absl::Span<const uint128_t> inputs, PxVector& values,
                          const PxVector& pp, PxVector::Helper& h,
                          uint64_t num_threads) {
  if (num_bins_ == 1) {
    Paxos<IdxType> paxos;
    paxos.Init(1, paxos_param_, seed_);

    paxos.Decode(inputs, values, pp, h);
    return;
  }

  num_threads = std::max<uint64_t>(num_threads, 1ull);

  std::vector<std::thread> thrds(num_threads - 1);
  auto routine = [&](uint64_t i) {
    auto begin = (inputs.size() * i) / num_threads;
    auto end = (inputs.size() * (i + 1)) / num_threads;
    absl::Span<const uint128_t> in =
        absl::MakeSpan(inputs.begin() + begin, inputs.begin() + end);
    auto va = values.subspan(begin, end - begin);

    ImplDecodeBatch<IdxType>(in, va, pp, h);
  };

  for (uint64_t i = 0; i < thrds.size(); ++i) {
    thrds[i] = std::thread(routine, i);
  }

  routine(thrds.size());

  for (uint64_t i = 0; i < thrds.size(); ++i) {
    thrds[i].join();
  }
}

}  // namespace psi::rr22::okvs
