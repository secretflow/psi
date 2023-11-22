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

#include "psi/psi/utils/inner_join.h"

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <future>
#include <string>
#include <unordered_map>
#include <vector>

#include "arrow/api.h"
#include "arrow/csv/api.h"
#include "arrow/io/api.h"
#include "arrow/ipc/api.h"
#include "external/com_github_gabime_spdlog/spdlog/include/spdlog/spdlog.h"
#include "spdlog/spdlog.h"
#include "yacl/base/exception.h"

#include "psi/psi/utils/inner_join.h"
#include "psi/psi/utils/utils.h"

#include "psi/proto/psi.pb.h"

namespace psi::psi {

constexpr char kInnerJoinKeyCount[] = "psi_inner_join_cnt";
constexpr char kInnerJoinFirstIndex[] = "psi_innner_join_first_index";
constexpr char kLinkTag[] = "PSI:INNER_JOIN_SYNC_CNT";

v2::InnerJoinConfig BuildInnerJoinConfig(const v2::PsiConfig& psi_config,
                                         const std::filesystem::path& root) {
  v2::InnerJoinConfig inner_join_config;

  inner_join_config.set_role(psi_config.protocol_config().role());

  for (const auto& key : psi_config.keys()) {
    *inner_join_config.add_keys() = key;
  }

  std::string prefix = psi_config.protocol_config().role() == v2::ROLE_RECEIVER
                           ? "receiver_"
                           : "sender_";

  std::filesystem::path sorted_input_path =
      root / (prefix + "sorted_input.csv");
  std::filesystem::path unique_input_keys_cnt_path =
      root / (prefix + "unique_input_keys_cnt.csv");
  std::filesystem::path self_intersection_cnt_path =
      root / (prefix + "self_intersection_cnt.csv");
  std::filesystem::path peer_intersection_cnt_path =
      root / (prefix + "peer_intersection_cnt.csv");

  inner_join_config.set_input_path(psi_config.input_config().path());
  inner_join_config.set_output_path(psi_config.output_config().path());

  inner_join_config.set_sorted_input_path(sorted_input_path.string());
  inner_join_config.set_unique_input_keys_cnt_path(
      unique_input_keys_cnt_path.string());
  inner_join_config.set_self_intersection_cnt_path(
      self_intersection_cnt_path.string());
  inner_join_config.set_peer_intersection_cnt_path(
      peer_intersection_cnt_path.string());

  return inner_join_config;
}

void InnerJoinGenerateSortedInput(const v2::InnerJoinConfig& config) {
  YACL_ENFORCE(std::filesystem::exists(config.input_path()),
               "Input file {} doesn't exist.", config.input_path());
  std::vector<std::string> keys(config.keys().begin(), config.keys().end());
  MultiKeySort(config.input_path(), config.sorted_input_path(), keys);
}

void InnerJoinGenerateUniqueInputKeysCnt(const v2::InnerJoinConfig& config) {
  YACL_ENFORCE(std::filesystem::exists(config.sorted_input_path()),
               "Sorted input file {} doesn't exist.",
               config.sorted_input_path());

  arrow::io::IOContext io_context = arrow::io::default_io_context();
  std::shared_ptr<arrow::io::ReadableFile> infile;
  infile = arrow::io::ReadableFile::Open(config.sorted_input_path(),
                                         arrow::default_memory_pool())
               .ValueOrDie();
  auto read_options = arrow::csv::ReadOptions::Defaults();
  auto parse_options = arrow::csv::ParseOptions::Defaults();
  auto convert_options = arrow::csv::ConvertOptions::Defaults();

  std::vector<std::string> keys(config.keys().begin(), config.keys().end());

  convert_options.include_columns = keys;

  auto reader =
      arrow::csv::StreamingReader::Make(io_context, infile, read_options,
                                        parse_options, convert_options)
          .ValueOrDie();

  const std::shared_ptr<arrow::Schema>& input_schema = reader->schema();

  std::vector<std::shared_ptr<arrow::Field>> output_fields;
  for (int i = 0; i < input_schema->num_fields(); i++) {
    output_fields.emplace_back(input_schema->field(i));
  }
  output_fields.emplace_back(arrow::field(kInnerJoinKeyCount, arrow::int64()));
  output_fields.emplace_back(
      arrow::field(kInnerJoinFirstIndex, arrow::int64()));

  std::shared_ptr<arrow::Schema> output_schema = arrow::schema(output_fields);

  // NOTE(junfeng): This is a hack to write the header without quotation masks
  // to make YACL csv utils happy.
  // Should be fixed as soon as possible.
  {
    std::ofstream file(config.unique_input_keys_cnt_path());

    for (int i = 0; i < output_schema->num_fields(); i++) {
      file << output_schema->field(i)->name();
      if (i != output_schema->num_fields() - 1) {
        file << ",";
      }
    }

    file << "\n";

    file.close();
  }
  std::shared_ptr<arrow::io::FileOutputStream> outfile =
      arrow::io::FileOutputStream::Open(config.unique_input_keys_cnt_path(),
                                        true)
          .ValueOrDie();
  auto write_options = arrow::csv::WriteOptions::Defaults();
  write_options.include_header = false;

  auto writer = arrow::csv::MakeCSVWriter(outfile, output_schema, write_options)
                    .ValueOrDie();

  std::shared_ptr<arrow::RecordBatch> batch;
  size_t cnt = 0;
  std::vector<std::shared_ptr<arrow::Scalar>> previous_keys;
  int64_t idx = 0;
  int64_t first_appearance = 0;

  while (true) {
    arrow::Status status = reader->ReadNext(&batch);

    if (!status.ok()) {
      YACL_THROW("Read csv error.");
    }

    if (batch == NULL) {
      break;
    }

    std::vector<std::unique_ptr<arrow::ArrayBuilder>> output_array_builders;
    output_array_builders.reserve(output_fields.size());

    for (auto field : output_fields) {
      output_array_builders.emplace_back(
          arrow::MakeBuilder(field->type()).ValueOrDie());
    }

    std::vector<std::shared_ptr<arrow::Array>> input_data = batch->columns();
    int num_rows = batch->num_rows();
    int num_cols = input_data.size();

    for (int i = 0; i < num_rows; i++) {
      std::vector<std::shared_ptr<arrow::Scalar>> current_keys;
      current_keys.reserve(input_data.size());
      for (int j = 0; j < num_cols; j++) {
        current_keys.emplace_back(input_data[j]->GetScalar(i).ValueOrDie());
      }

      if (previous_keys.empty()) {
        previous_keys = current_keys;
        cnt += 1;
      } else {
        bool is_same = true;

        for (int k = 0; k < num_cols; k++) {
          if (!previous_keys[k]->Equals(current_keys[k])) {
            is_same = false;
            break;
          }
        }

        if (is_same) {
          cnt += 1;
        } else {
          for (int p = 0; p < num_cols; p++) {
            YACL_ENFORCE(output_array_builders[p]
                             ->AppendScalar(*(previous_keys[p]))
                             .ok());
          }
          YACL_ENFORCE(output_array_builders[num_cols]
                           ->AppendScalar(arrow::Int64Scalar(cnt))
                           .ok());
          YACL_ENFORCE(output_array_builders[num_cols + 1]
                           ->AppendScalar(arrow::Int64Scalar(first_appearance))
                           .ok());

          previous_keys = current_keys;
          cnt = 1;
          first_appearance = idx;
        }
      }
      idx++;
    }

    std::vector<std::shared_ptr<arrow::Array>> output_arrays;
    output_arrays.reserve(output_fields.size());
    for (auto& builder : output_array_builders) {
      output_arrays.emplace_back(builder->Finish().ValueOrDie());
    }

    if (output_arrays[0]->length() > 0) {
      std::shared_ptr<arrow::RecordBatch> output_batch =
          arrow::RecordBatch::Make(output_schema, output_arrays[0]->length(),
                                   output_arrays);
      if (!writer->WriteRecordBatch(*output_batch).ok()) {
        YACL_THROW("writer WriteRecordBatch failed.");
      }
    }
  }

  // last cnt
  std::vector<std::unique_ptr<arrow::ArrayBuilder>> output_array_builders;
  output_array_builders.reserve(output_fields.size());

  for (auto field : output_fields) {
    output_array_builders.emplace_back(
        arrow::MakeBuilder(field->type()).ValueOrDie());
  }
  for (int p = 0; p < input_schema->num_fields(); p++) {
    YACL_ENFORCE(
        output_array_builders[p]->AppendScalar(*(previous_keys[p])).ok());
  }
  YACL_ENFORCE(output_array_builders[input_schema->num_fields()]
                   ->AppendScalar(arrow::Int64Scalar(cnt))
                   .ok());
  YACL_ENFORCE(output_array_builders[input_schema->num_fields() + 1]
                   ->AppendScalar(arrow::Int64Scalar(first_appearance))
                   .ok());

  std::vector<std::shared_ptr<arrow::Array>> output_arrays;
  output_arrays.reserve(output_fields.size());
  for (auto& builder : output_array_builders) {
    output_arrays.emplace_back(builder->Finish().ValueOrDie());
  }

  if (output_arrays[0]->length() > 0) {
    std::shared_ptr<arrow::RecordBatch> output_batch = arrow::RecordBatch::Make(
        output_schema, output_arrays[0]->length(), output_arrays);
    if (!writer->WriteRecordBatch(*output_batch).ok()) {
      YACL_THROW("writer WriteRecordBatch failed.");
    }
  }

  if (!writer->Close().ok()) {
    YACL_THROW("writer Close failed.");
  }
  if (!outfile->Close().ok()) {
    YACL_THROW("outfile Close failed.");
  }
  if (!infile->Close().ok()) {
    YACL_THROW("infile Close failed.");
  }
}

void SendSelfCnt(const std::shared_ptr<yacl::link::Context>& link_ctx,
                 const v2::InnerJoinConfig& config) {
  YACL_ENFORCE(link_ctx->WorldSize() == 2);
  YACL_ENFORCE(std::filesystem::exists(config.self_intersection_cnt_path()),
               "Self intersection cnt file {} doesn't exist.",
               config.self_intersection_cnt_path());

  std::shared_ptr<arrow::io::ReadableFile> infile;
  infile = arrow::io::ReadableFile::Open(config.self_intersection_cnt_path(),
                                         arrow::default_memory_pool())
               .ValueOrDie();

  auto read_options = arrow::csv::ReadOptions::Defaults();
  auto parse_options = arrow::csv::ParseOptions::Defaults();
  auto convert_options = arrow::csv::ConvertOptions::Defaults();
  convert_options.include_columns =
      std::vector<std::string>{kInnerJoinKeyCount};

  arrow::io::IOContext io_context = arrow::io::default_io_context();

  auto reader =
      arrow::csv::StreamingReader::Make(io_context, infile, read_options,
                                        parse_options, convert_options)
          .ValueOrDie();

  std::shared_ptr<arrow::RecordBatch> batch;

  while (true) {
    arrow::Status status = reader->ReadNext(&batch);

    if (!status.ok()) {
      YACL_THROW("Read csv error.");
    }

    if (batch == NULL) {
      // send an empty record to indicate the end of sending.
      link_ctx->SendAsync(link_ctx->NextRank(), "", kLinkTag);
      break;
    } else {
      auto cnt_array =
          std::static_pointer_cast<arrow::Int64Array>(batch->column(0));

      std::shared_ptr<arrow::Buffer> buffer = cnt_array->values();
      link_ctx->SendAsync(
          link_ctx->NextRank(),
          yacl::ByteContainerView(buffer->data(), buffer->size()), kLinkTag);
    }
  }

  if (!infile->Close().ok()) {
    YACL_THROW("infile Close failed.");
  }
}

void RecvPeerCnt(const std::shared_ptr<yacl::link::Context>& link_ctx,
                 const v2::InnerJoinConfig& config) {
  YACL_ENFORCE(link_ctx->WorldSize() == 2);

  std::shared_ptr<arrow::io::FileOutputStream> outfile =
      arrow::io::FileOutputStream::Open(config.peer_intersection_cnt_path())
          .ValueOrDie();
  auto write_options = arrow::csv::WriteOptions::Defaults();
  write_options.include_header = true;

  std::shared_ptr<arrow::Schema> schema =
      arrow::schema({arrow::field(kInnerJoinKeyCount, arrow::int64())});

  auto writer =
      arrow::csv::MakeCSVWriter(outfile, schema, write_options).ValueOrDie();

  while (true) {
    yacl::Buffer buffer = link_ctx->Recv(link_ctx->NextRank(), kLinkTag);

    int buffer_size = buffer.size();

    if (buffer_size == 0) {
      // Receiving empty record which indicates the end of peer sending.
      break;
    }

    int64_t* buffer_data = buffer.data<int64_t>();

    std::shared_ptr<arrow::ArrayBuilder> builder =
        arrow::MakeBuilder(arrow::int64()).ValueOrDie();

    for (size_t i = 0; i < buffer_size / sizeof(int64_t); i++) {
      YACL_ENFORCE(
          builder->AppendScalar(arrow::Int64Scalar(buffer_data[i])).ok());
    }

    std::vector<std::shared_ptr<arrow::Array>> output_arrays;
    output_arrays.emplace_back(builder->Finish().ValueOrDie());

    if (output_arrays[0]->length() > 0) {
      std::shared_ptr<arrow::RecordBatch> output_batch =
          arrow::RecordBatch::Make(schema, output_arrays[0]->length(),
                                   output_arrays);
      if (!writer->WriteRecordBatch(*output_batch).ok()) {
        YACL_THROW("writer WriteRecordBatch failed.");
      }
    }
  }

  if (!outfile->Close().ok()) {
    YACL_THROW("outfile Close failed.");
  }
}

void InnerJoinSyncIntersectionCnt(
    const std::shared_ptr<yacl::link::Context>& link_ctx,
    const v2::InnerJoinConfig& config) {
  YACL_ENFORCE(link_ctx->WorldSize() == 2);

  std::future<void> f_send_self_cnt =
      std::async([&] { return SendSelfCnt(link_ctx, config); });
  std::future<void> f_recv_peer_cnt =
      std::async([&] { return RecvPeerCnt(link_ctx, config); });

  std::exception_ptr send_self_cnt_exptr = nullptr;
  std::exception_ptr recv_peer_cnt_exptr = nullptr;

  try {
    f_send_self_cnt.get();
  } catch (const std::exception& e) {
    send_self_cnt_exptr = std::current_exception();
    SPDLOG_ERROR("Error in send self cnt: {}", e.what());
  }

  try {
    f_recv_peer_cnt.get();
  } catch (const std::exception& e) {
    recv_peer_cnt_exptr = std::current_exception();
    SPDLOG_ERROR("Error in recv peer cnt: {}", e.what());
  }

  if (send_self_cnt_exptr) {
    std::rethrow_exception(send_self_cnt_exptr);
  }
  if (recv_peer_cnt_exptr) {
    std::rethrow_exception(recv_peer_cnt_exptr);
  }
}

// Read next record.
std::vector<std::shared_ptr<arrow::Scalar>> ReadNextRecord(
    const std::shared_ptr<arrow::ipc::RecordBatchReader>& reader,
    std::shared_ptr<arrow::RecordBatch>* batch, int64_t* read_batch_index,
    const std::vector<std::string>& cols) {
  std::vector<std::shared_ptr<arrow::Scalar>> res;

  if (*batch == NULL || *read_batch_index >= (*batch)->num_rows()) {
    arrow::Status status = reader->ReadNext(batch);

    if (!status.ok()) {
      YACL_THROW("Read csv error.");
    }

    if (*batch == NULL) {
      SPDLOG_WARN("Reach the end of csv.");
      return res;
    }

    *read_batch_index = 0;
  }

  int64_t previous_read_batch_index = *read_batch_index;
  *read_batch_index = previous_read_batch_index + 1;

  for (const auto& col : cols) {
    res.emplace_back((*batch)
                         ->GetColumnByName(col)
                         ->GetScalar(previous_read_batch_index)
                         .ValueOrDie());
  }

  return res;
}

void GenerateOutputForSingle(
    const std::shared_ptr<arrow::ipc::RecordBatchWriter>& writer,
    const std::shared_ptr<arrow::csv::StreamingReader>& sorted_input_reader,
    const std::vector<std::string>& key_col_names,
    const std::vector<std::shared_ptr<arrow::Scalar>>& target_keys,
    int64_t self_cnt, int64_t peer_cnt, v2::Role role,
    std::shared_ptr<arrow::RecordBatch>* sorted_input_batch,
    int64_t* read_batch_index) {
  bool found = false;

  // found target keys;
  while (!found) {
    if (*sorted_input_batch == NULL ||
        *read_batch_index >= (*sorted_input_batch)->num_rows()) {
      arrow::Status status = sorted_input_reader->ReadNext(sorted_input_batch);

      if (!status.ok()) {
        YACL_THROW("Read csv error.");
      }

      if (*sorted_input_batch == NULL) {
        YACL_THROW("sorted_input_reader reach the end.");
      }

      *read_batch_index = 0;
    }

    for (; *read_batch_index < (*sorted_input_batch)->num_rows();
         *read_batch_index = *read_batch_index + 1) {
      std::vector<std::shared_ptr<arrow::Scalar>> current_keys;
      current_keys.reserve(key_col_names.size());
      for (const std::string& col_name : key_col_names) {
        current_keys.emplace_back((*sorted_input_batch)
                                      ->GetColumnByName(col_name)
                                      ->GetScalar(*read_batch_index)
                                      .ValueOrDie());
      }

      bool equal = true;
      for (size_t i = 0; i < key_col_names.size(); i++) {
        if (!current_keys[i]->Equals(target_keys[i])) {
          equal = false;
          break;
        }
      }

      if (equal) {
        found = true;
        break;
      }
    }
  }

  // collect all rows with the same keys.
  std::vector<std::vector<std::shared_ptr<arrow::Scalar>>> output_scalars;
  output_scalars.reserve(sorted_input_reader->schema()->num_fields());

  for (int i = 0; i < sorted_input_reader->schema()->num_fields(); i++) {
    output_scalars.emplace_back(std::vector<std::shared_ptr<arrow::Scalar>>());
  }

  bool finished = false;
  while (!finished) {
    if (*sorted_input_batch == NULL ||
        *read_batch_index >= (*sorted_input_batch)->num_rows()) {
      arrow::Status status = sorted_input_reader->ReadNext(sorted_input_batch);

      if (!status.ok()) {
        YACL_THROW("Read csv error.");
      }

      if (*sorted_input_batch == NULL) {
        break;
      }

      *read_batch_index = 0;
    }

    for (; *read_batch_index < (*sorted_input_batch)->num_rows();
         *read_batch_index = *read_batch_index + 1) {
      std::vector<std::shared_ptr<arrow::Scalar>> current_keys;
      current_keys.reserve(key_col_names.size());
      for (const std::string& col_name : key_col_names) {
        current_keys.emplace_back((*sorted_input_batch)
                                      ->GetColumnByName(col_name)
                                      ->GetScalar(*read_batch_index)
                                      .ValueOrDie());
      }

      bool equal = true;
      for (size_t i = 0; i < key_col_names.size(); i++) {
        if (!current_keys[i]->Equals(target_keys[i])) {
          equal = false;
          break;
        }
      }

      if (equal) {
        for (int i = 0; i < sorted_input_reader->schema()->num_fields(); i++) {
          output_scalars[i].emplace_back((*sorted_input_batch)
                                             ->column(i)
                                             ->GetScalar(*read_batch_index)
                                             .ValueOrDie());
        }
      } else {
        finished = true;
        break;
      }
    }
  }

  YACL_ENFORCE_EQ(static_cast<int64_t>(output_scalars[0].size()), self_cnt,
                  "self_cnt doesn't match the actual sorted input.");

  // Write to output
  std::vector<std::unique_ptr<arrow::ArrayBuilder>> output_array_builders;
  output_array_builders.reserve(sorted_input_reader->schema()->num_fields());

  for (int i = 0; i < sorted_input_reader->schema()->num_fields(); i++) {
    output_array_builders.emplace_back(
        arrow::MakeBuilder(sorted_input_reader->schema()->field(i)->type())
            .ValueOrDie());
  }

  if (role == v2::ROLE_RECEIVER) {
    for (int i = 0; i < self_cnt; i++) {
      for (int j = 0; j < peer_cnt; j++) {
        for (int k = 0; k < sorted_input_reader->schema()->num_fields(); k++) {
          YACL_ENFORCE(output_array_builders[k]
                           ->AppendScalar(*(output_scalars[k][i]))
                           .ok());
        }
      }
    }
  } else {
    for (int i = 0; i < peer_cnt; i++) {
      for (int j = 0; j < self_cnt; j++) {
        for (int k = 0; k < sorted_input_reader->schema()->num_fields(); k++) {
          YACL_ENFORCE(output_array_builders[k]
                           ->AppendScalar(*(output_scalars[k][j]))
                           .ok());
        }
      }
    }
  }

  std::vector<std::shared_ptr<arrow::Array>> output_arrays;
  output_arrays.reserve(sorted_input_reader->schema()->num_fields());

  for (auto& builder : output_array_builders) {
    output_arrays.emplace_back(builder->Finish().ValueOrDie());
  }

  if (output_arrays[0]->length() > 0) {
    std::shared_ptr<arrow::RecordBatch> output_batch =
        arrow::RecordBatch::Make(sorted_input_reader->schema(),
                                 output_arrays[0]->length(), output_arrays);
    if (!writer->WriteRecordBatch(*output_batch).ok()) {
      YACL_THROW("writer WriteRecordBatch failed.");
    }
  }
}

void InnerJoinGenerateIntersection(const v2::InnerJoinConfig& config) {
  YACL_ENFORCE(std::filesystem::exists(config.sorted_input_path()),
               "Sorted input file {} doesn't exist.",
               config.sorted_input_path());

  YACL_ENFORCE(std::filesystem::exists(config.self_intersection_cnt_path()),
               "Sorted input file {} doesn't exist.",
               config.self_intersection_cnt_path());

  YACL_ENFORCE(std::filesystem::exists(config.peer_intersection_cnt_path()),
               "Sorted input file {} doesn't exist.",
               config.peer_intersection_cnt_path());

  std::shared_ptr<arrow::io::ReadableFile> self_intersection_cnt_infile;
  self_intersection_cnt_infile =
      arrow::io::ReadableFile::Open(config.self_intersection_cnt_path(),
                                    arrow::default_memory_pool())
          .ValueOrDie();
  std::shared_ptr<arrow::io::ReadableFile> peer_intersection_cnt_infile;
  peer_intersection_cnt_infile =
      arrow::io::ReadableFile::Open(config.peer_intersection_cnt_path(),
                                    arrow::default_memory_pool())
          .ValueOrDie();
  std::shared_ptr<arrow::io::ReadableFile> sorted_input_infile;
  sorted_input_infile =
      arrow::io::ReadableFile::Open(config.sorted_input_path(),
                                    arrow::default_memory_pool())
          .ValueOrDie();

  auto read_options = arrow::csv::ReadOptions::Defaults();
  auto parse_options = arrow::csv::ParseOptions::Defaults();
  auto self_intersection_cnt_convert_options =
      arrow::csv::ConvertOptions::Defaults();

  std::vector<std::string> keys(config.keys().begin(), config.keys().end());
  keys.emplace_back(kInnerJoinKeyCount);
  self_intersection_cnt_convert_options.include_columns = keys;
  auto peer_intersection_cnt_convert_options =
      arrow::csv::ConvertOptions::Defaults();
  peer_intersection_cnt_convert_options.include_columns =
      std::vector<std::string>{kInnerJoinKeyCount};
  auto sorted_input_convert_options = arrow::csv::ConvertOptions::Defaults();

  arrow::io::IOContext self_intersection_cnt_io_context =
      arrow::io::default_io_context();
  arrow::io::IOContext peer_intersection_cnt_io_context =
      arrow::io::default_io_context();
  arrow::io::IOContext sorted_input_io_context =
      arrow::io::default_io_context();

  auto self_intersection_cnt_reader =
      arrow::csv::StreamingReader::Make(
          self_intersection_cnt_io_context, self_intersection_cnt_infile,
          read_options, parse_options, self_intersection_cnt_convert_options)
          .ValueOrDie();
  auto peer_intersection_cnt_reader =
      arrow::csv::StreamingReader::Make(
          peer_intersection_cnt_io_context, peer_intersection_cnt_infile,
          read_options, parse_options, peer_intersection_cnt_convert_options)
          .ValueOrDie();
  auto sorted_input_reader =
      arrow::csv::StreamingReader::Make(
          sorted_input_io_context, sorted_input_infile, read_options,
          parse_options, sorted_input_convert_options)
          .ValueOrDie();

  std::shared_ptr<arrow::RecordBatch> self_intersection_cnt_batch;
  std::shared_ptr<arrow::RecordBatch> peer_intersection_cnt_batch;
  std::shared_ptr<arrow::RecordBatch> sorted_input_batch;

  std::shared_ptr<arrow::io::FileOutputStream> outfile =
      arrow::io::FileOutputStream::Open(config.output_path()).ValueOrDie();
  auto write_options = arrow::csv::WriteOptions::Defaults();
  write_options.include_header = true;

  auto writer = arrow::csv::MakeCSVWriter(
                    outfile, sorted_input_reader->schema(), write_options)
                    .ValueOrDie();

  int64_t input_batch_index = 0;
  int64_t peer_cnt_batch_index = 0;

  // Iterate self_intersection_cnt;
  while (true) {
    arrow::Status status =
        self_intersection_cnt_reader->ReadNext(&self_intersection_cnt_batch);

    if (!status.ok()) {
      YACL_THROW("Read csv error.");
    }

    if (self_intersection_cnt_batch == NULL) {
      break;
    }

    std::vector<std::shared_ptr<arrow::Array>> self_intersection_cnt_data =
        self_intersection_cnt_batch->columns();

    auto self_count_array = std::static_pointer_cast<arrow::Int64Array>(
        self_intersection_cnt_data[config.keys().size()]);

    for (int i = 0; i < self_intersection_cnt_batch->num_rows(); i++) {
      std::vector<std::shared_ptr<arrow::Scalar>> current_keys;
      current_keys.reserve(config.keys().size());
      for (int j = 0; j < config.keys().size(); j++) {
        current_keys.emplace_back(
            self_intersection_cnt_data[j]->GetScalar(i).ValueOrDie());
      }

      int64_t current_self_count = self_count_array->Value(i);

      std::vector<std::shared_ptr<arrow::Scalar>> record = ReadNextRecord(
          peer_intersection_cnt_reader, &peer_intersection_cnt_batch,
          &peer_cnt_batch_index, std::vector<std::string>{kInnerJoinKeyCount});

      int64_t peer_count =
          std::dynamic_pointer_cast<arrow::Int64Scalar>(record[0])->value;

      YACL_ENFORCE_NE(peer_count, -1);

      std::vector<std::string> psi_keys(config.keys().begin(),
                                        config.keys().end());

      GenerateOutputForSingle(writer, sorted_input_reader, psi_keys,
                              current_keys, current_self_count, peer_count,
                              config.role(), &sorted_input_batch,
                              &input_batch_index);
    }
  }

  YACL_ENFORCE(ReadNextRecord(peer_intersection_cnt_reader,
                              &peer_intersection_cnt_batch,
                              &peer_cnt_batch_index,
                              std::vector<std::string>{kInnerJoinKeyCount})
                   .empty());

  if (!self_intersection_cnt_infile->Close().ok()) {
    YACL_THROW("self_intersection_cnt_infile Close failed.");
  }
  if (!peer_intersection_cnt_infile->Close().ok()) {
    YACL_THROW("peer_intersection_cnt_infile Close failed.");
  }
  if (!sorted_input_infile->Close().ok()) {
    YACL_THROW("sorted_input_infile Close failed.");
  }
  if (!outfile->Close().ok()) {
    YACL_THROW("outfile Close failed.");
  }
}

void InnerJoinGenerateDifference(const v2::InnerJoinConfig& config) {
  YACL_ENFORCE(std::filesystem::exists(config.sorted_input_path()),
               "Sorted input file {} doesn't exist.",
               config.sorted_input_path());

  YACL_ENFORCE(std::filesystem::exists(config.self_intersection_cnt_path()),
               "Sorted input file {} doesn't exist.",
               config.self_intersection_cnt_path());

  std::shared_ptr<arrow::io::ReadableFile> self_intersection_cnt_infile;
  self_intersection_cnt_infile =
      arrow::io::ReadableFile::Open(config.self_intersection_cnt_path(),
                                    arrow::default_memory_pool())
          .ValueOrDie();
  std::shared_ptr<arrow::io::ReadableFile> sorted_input_infile;
  sorted_input_infile =
      arrow::io::ReadableFile::Open(config.sorted_input_path(),
                                    arrow::default_memory_pool())
          .ValueOrDie();

  auto read_options = arrow::csv::ReadOptions::Defaults();
  auto parse_options = arrow::csv::ParseOptions::Defaults();
  auto self_intersection_cnt_convert_options =
      arrow::csv::ConvertOptions::Defaults();
  auto sorted_input_convert_options = arrow::csv::ConvertOptions::Defaults();

  arrow::io::IOContext self_intersection_cnt_io_context =
      arrow::io::default_io_context();
  arrow::io::IOContext sorted_input_io_context =
      arrow::io::default_io_context();

  auto self_intersection_cnt_reader =
      arrow::csv::StreamingReader::Make(
          self_intersection_cnt_io_context, self_intersection_cnt_infile,
          read_options, parse_options, self_intersection_cnt_convert_options)
          .ValueOrDie();
  auto sorted_input_reader =
      arrow::csv::StreamingReader::Make(
          sorted_input_io_context, sorted_input_infile, read_options,
          parse_options, sorted_input_convert_options)
          .ValueOrDie();

  std::shared_ptr<arrow::RecordBatch> self_intersection_cnt_batch;
  std::shared_ptr<arrow::RecordBatch> sorted_input_batch;

  std::shared_ptr<arrow::io::FileOutputStream> outfile =
      arrow::io::FileOutputStream::Open(config.output_path()).ValueOrDie();
  auto write_options = arrow::csv::WriteOptions::Defaults();
  write_options.include_header = true;

  auto writer = arrow::csv::MakeCSVWriter(
                    outfile, sorted_input_reader->schema(), write_options)
                    .ValueOrDie();

  int64_t self_intersection_cnt_batch_index = 0;

  std::vector<std::shared_ptr<arrow::Scalar>> record = ReadNextRecord(
      self_intersection_cnt_reader, &self_intersection_cnt_batch,
      &self_intersection_cnt_batch_index,
      std::vector<std::string>{kInnerJoinFirstIndex, kInnerJoinKeyCount});

  int64_t idx = 0;

  while (true) {
    arrow::Status status = sorted_input_reader->ReadNext(&sorted_input_batch);

    if (!status.ok()) {
      YACL_THROW("Read csv error.");
    }

    if (sorted_input_batch == NULL) {
      break;
    }

    std::vector<std::shared_ptr<arrow::Array>> sorted_input_data =
        sorted_input_batch->columns();

    std::vector<std::unique_ptr<arrow::ArrayBuilder>> output_array_builders;
    output_array_builders.reserve(sorted_input_reader->schema()->num_fields());

    for (int i = 0; i < sorted_input_reader->schema()->num_fields(); i++) {
      output_array_builders.emplace_back(
          arrow::MakeBuilder(sorted_input_reader->schema()->field(i)->type())
              .ValueOrDie());
    }

    for (int i = 0; i < sorted_input_batch->num_rows(); i++) {
      if (record.empty() ||
          idx <
              std::dynamic_pointer_cast<arrow::Int64Scalar>(record[0])->value) {
        for (int j = 0; j < sorted_input_reader->schema()->num_fields(); j++) {
          YACL_ENFORCE(
              output_array_builders[j]
                  ->AppendScalar(*(
                      sorted_input_batch->column(j)->GetScalar(i).ValueOrDie()))
                  .ok());
        }
      }

      if (!record.empty() &&
          idx ==
              (std::dynamic_pointer_cast<arrow::Int64Scalar>(record[0])->value +
               std::dynamic_pointer_cast<arrow::Int64Scalar>(record[1])->value -
               1)) {
        record = ReadNextRecord(
            self_intersection_cnt_reader, &self_intersection_cnt_batch,
            &self_intersection_cnt_batch_index,
            std::vector<std::string>{kInnerJoinFirstIndex, kInnerJoinKeyCount});
      }

      idx += 1;
    }

    std::vector<std::shared_ptr<arrow::Array>> output_arrays;
    output_arrays.reserve(sorted_input_reader->schema()->num_fields());

    for (auto& builder : output_array_builders) {
      output_arrays.emplace_back(builder->Finish().ValueOrDie());
    }

    if (output_arrays[0]->length() > 0) {
      std::shared_ptr<arrow::RecordBatch> output_batch =
          arrow::RecordBatch::Make(sorted_input_reader->schema(),
                                   output_arrays[0]->length(), output_arrays);
      if (!writer->WriteRecordBatch(*output_batch).ok()) {
        YACL_THROW("writer WriteRecordBatch failed.");
      }
    }
  }
}

}  // namespace psi::psi
