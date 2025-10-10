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

#include "experiment/psi/threshold_ecdh_psi/threshold_ecdh_psi_cache.h"

namespace psi::ecdh {

namespace {

std::filesystem::path GetCacheFileName(
    const std::filesystem::path& cache_path) {
  return cache_path / "threshold_ecdh_psi_cache.bin";
}

std::filesystem::path GetCacheMetaName(
    const std::filesystem::path& cache_path) {
  return cache_path / "threshold_ecdh_psi_cache.meta";
}

}  // namespace

ThresholdEcdhPsiCacheProvider::ThresholdEcdhPsiCacheProvider(
    const std::filesystem::path& file_path, size_t batch_size)
    : batch_size_(batch_size) {
  auto data_file = GetCacheFileName(file_path);
  YACL_ENFORCE(std::filesystem::exists(data_file), "{} not exists",
               data_file.string());

  in_ = std::ifstream(data_file, std::ios::binary);

  meta_path_ = GetCacheMetaName(file_path);
  YACL_ENFORCE(std::filesystem::exists(meta_path_), "{} not exists",
               meta_path_.string());

  LoadMeta();

  auto file_item_cnt =
      std::filesystem::file_size(data_file) / sizeof(ThresholdEcdhPsiCacheItem);
  YACL_ENFORCE(file_item_cnt == cache_cnt_,
               "file item count {}  mismatch meta record count {}",
               file_item_cnt, cache_cnt_);
}

IShuffledBatchProvider::ShuffledBatch
ThresholdEcdhPsiCacheProvider::ReadNextShuffledBatch() {
  ShuffledBatch shuffled_batch;

  if (read_cnt_ >= cache_cnt_) {
    return shuffled_batch;
  }

  size_t count = std::min(batch_size_, cache_cnt_ - read_cnt_);

  if (count > 0) {
    auto items = ReadData(count);
    for (auto& item : items) {
      shuffled_batch.batch_indices.emplace_back(item.origin_index);
      shuffled_batch.shuffled_indices.emplace_back(item.shuffle_index);
      shuffled_batch.dup_cnts.emplace_back(item.dup_cnt);
    }
    read_cnt_ += count;
  }

  return shuffled_batch;
}

std::vector<ThresholdEcdhPsiCacheItem> ThresholdEcdhPsiCacheProvider::ReadData(
    size_t read_count) {
  std::vector<ThresholdEcdhPsiCacheItem> ret(read_count);

  in_.read(reinterpret_cast<char*>(ret.data()),
           read_count * sizeof(ThresholdEcdhPsiCacheItem));

  return ret;
}

void ThresholdEcdhPsiCacheProvider::LoadMeta() {
  std::ifstream meta_stream(meta_path_, std::ios::binary);
  meta_stream.read(reinterpret_cast<char*>(&cache_cnt_), sizeof(cache_cnt_));
}

ThresholdEcdhPsiCache::ThresholdEcdhPsiCache(
    const std::filesystem::path& file_path) {
  meta_path_ = GetCacheMetaName(file_path);
  out_stream_ =
      io::BuildOutputStream(io::FileIoOptions(GetCacheFileName(file_path)));
}

void ThresholdEcdhPsiCache::SaveData(size_t index, size_t shuffle_index,
                                     uint32_t dup_cnt) {
  ThresholdEcdhPsiCacheItem cache_item{
      .origin_index = static_cast<uint32_t>(index),
      .shuffle_index = static_cast<uint32_t>(shuffle_index),
      .dup_cnt = dup_cnt};
  out_stream_->Write(&cache_item, sizeof(ThresholdEcdhPsiCacheItem));
  cache_cnt_++;
}

void ThresholdEcdhPsiCache::Flush() {
  DumpMeta();
  out_stream_->Flush();
}

void ThresholdEcdhPsiCache::DumpMeta() {
  std::ofstream meta_stream(meta_path_, std::ios::binary);
  meta_stream.write(reinterpret_cast<const char*>(&cache_cnt_),
                    sizeof(cache_cnt_));
}

}  // namespace psi::ecdh