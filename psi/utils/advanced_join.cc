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

#include "psi/utils/advanced_join.h"

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <future>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "arrow/api.h"
#include "arrow/compute/api.h"
#include "arrow/csv/api.h"
#include "arrow/io/api.h"
#include "arrow/ipc/api.h"
#include "boost/uuid/uuid.hpp"
#include "boost/uuid/uuid_generators.hpp"
#include "boost/uuid/uuid_io.hpp"
#include "spdlog/spdlog.h"
#include "yacl/base/exception.h"

#include "psi/prelude.h"
#include "psi/utils/io.h"
#include "psi/utils/key.h"

#include "psi/proto/psi_v2.pb.h"

namespace psi {

constexpr char kAdvancedJoinKeyCount[] = "psi_advanced_join_cnt";
constexpr char kAdvancedJoinFirstIndex[] = "psi_advanced_join_first_index";
constexpr char kIntersectionLinkTag[] = "PSI:ADVANCED_JOIN_INTERSECTION_CNT";
constexpr char kMetaLinkTag[] = "PSI:ADVANCED_JOIN_META";

#define PSI_ARROW_GET_RESULT(value, maker)                                   \
  do {                                                                       \
    auto result = maker;                                                     \
    YACL_ENFORCE(result.ok(), "Arrow result failed: {}, status: {}", #maker, \
                 result.status().message());                                 \
    value = std::move(*result);                                              \
  } while (0)

AdvancedJoinConfig BuildAdvancedJoinConfig(v2::PsiConfig::AdvancedJoinType type,
                                           v2::Role role, v2::Role left_side,
                                           const std::vector<std::string>& keys,
                                           const std::string& input_path,
                                           const std::string& output_path,
                                           const std::filesystem::path& root) {
  AdvancedJoinConfig advanced_join_config;

  advanced_join_config.type = type;
  YACL_ENFORCE_NE(advanced_join_config.type,
                  v2::PsiConfig::ADVANCED_JOIN_TYPE_UNSPECIFIED,
                  "a valid advanced join type must be provided.");

  advanced_join_config.role = role;
  YACL_ENFORCE_NE(advanced_join_config.role, v2::Role::ROLE_UNSPECIFIED,
                  "a valid role must be provided.");

  advanced_join_config.left_side = left_side;
  if (advanced_join_config.type !=
      v2::PsiConfig::ADVANCED_JOIN_TYPE_INNER_JOIN) {
    YACL_ENFORCE_NE(advanced_join_config.left_side, v2::Role::ROLE_UNSPECIFIED,
                    "a valid left side must be provided.");
  }

  advanced_join_config.keys = keys;

  boost::uuids::random_generator uuid_generator;
  std::string uuid_str = boost::uuids::to_string(uuid_generator());

  std::string prefix = fmt::format(
      "{}_{}_", role == v2::ROLE_RECEIVER ? "receiver" : "sender", uuid_str);
  std::filesystem::path sorted_input_path =
      root / (prefix + "advanced_join_sorted_input.csv");
  std::filesystem::path unique_input_keys_cnt_path =
      root / (prefix + "advanced_join_unique_input_keys_cnt.csv");
  std::filesystem::path self_intersection_cnt_path =
      root / (prefix + "advanced_join_self_intersection_cnt.csv");
  std::filesystem::path peer_intersection_cnt_path =
      root / (prefix + "advanced_join_peer_intersection_cnt.csv");
  std::filesystem::path difference_output_path =
      root / (prefix + "advanced_join_difference_output.csv");

  advanced_join_config.input_path = input_path;
  advanced_join_config.output_path = output_path;

  advanced_join_config.sorted_input_path = sorted_input_path.string();
  advanced_join_config.unique_input_keys_cnt_path =
      unique_input_keys_cnt_path.string();
  advanced_join_config.self_intersection_cnt_path =
      self_intersection_cnt_path.string();
  advanced_join_config.peer_intersection_cnt_path =
      peer_intersection_cnt_path.string();
  advanced_join_config.difference_output_path = difference_output_path.string();

  return advanced_join_config;
}

AdvancedJoinConfig BuildAdvancedJoinConfig(const v2::PsiConfig& psi_config,
                                           const std::filesystem::path& root) {
  std::vector<std::string> keys = std::vector<std::string>(
      psi_config.keys().begin(), psi_config.keys().end());

  return BuildAdvancedJoinConfig(
      psi_config.advanced_join_type(), psi_config.protocol_config().role(),
      psi_config.left_side(), keys, psi_config.input_config().path(),
      psi_config.output_config().path(), root);
}

void AdvancedJoinGenerateSortedInput(const AdvancedJoinConfig& config) {
  YACL_ENFORCE(std::filesystem::exists(config.input_path),
               "Input file {} doesn't exist.", config.input_path);
  MultiKeySort(config.input_path, config.sorted_input_path, config.keys);
}

void AdvancedJoinPreprocess(AdvancedJoinConfig* config) {
  AdvancedJoinGenerateSortedInput(*config);

  YACL_ENFORCE(std::filesystem::exists(config->sorted_input_path),
               "Sorted input file {} doesn't exist.",
               config->sorted_input_path);

  arrow::io::IOContext io_context = arrow::io::default_io_context();
  std::shared_ptr<arrow::io::ReadableFile> infile;
  PSI_ARROW_GET_RESULT(
      infile, arrow::io::ReadableFile::Open(config->sorted_input_path,
                                            arrow::default_memory_pool()));

  auto read_options = arrow::csv::ReadOptions::Defaults();
  auto parse_options = arrow::csv::ParseOptions::Defaults();
  auto convert_options = arrow::csv::ConvertOptions::Defaults();

  convert_options.include_columns = config->keys;
  for (auto& key : config->keys) {
    convert_options.column_types[key] = arrow::utf8();
  }

  std::shared_ptr<arrow::csv::StreamingReader> reader;
  PSI_ARROW_GET_RESULT(reader, arrow::csv::StreamingReader::Make(
                                   io_context, infile, read_options,
                                   parse_options, convert_options));

  const std::shared_ptr<arrow::Schema>& input_schema = reader->schema();

  std::vector<std::shared_ptr<arrow::Field>> output_fields;
  for (int i = 0; i < input_schema->num_fields(); i++) {
    output_fields.emplace_back(input_schema->field(i));
  }
  output_fields.emplace_back(
      arrow::field(kAdvancedJoinKeyCount, arrow::int64()));
  output_fields.emplace_back(
      arrow::field(kAdvancedJoinFirstIndex, arrow::int64()));

  std::shared_ptr<arrow::Schema> output_schema = arrow::schema(output_fields);

  // NOTE(junfeng): This is a hack to write the header without quotation masks
  // to make YACL csv utils happy.
  // Should be fixed as soon as possible.
  {
    std::ofstream file(config->unique_input_keys_cnt_path);

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
      io::GetArrowOutputStream(config->unique_input_keys_cnt_path, true);
  auto write_options = arrow::csv::WriteOptions::Defaults();
  write_options.include_header = false;

  std::shared_ptr<arrow::ipc::RecordBatchWriter> writer;
  PSI_ARROW_GET_RESULT(
      writer, arrow::csv::MakeCSVWriter(outfile, output_schema, write_options));

  std::shared_ptr<arrow::RecordBatch> batch;
  int64_t self_total_cnt = 0;
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
      std::unique_ptr<arrow::ArrayBuilder> builder;
      PSI_ARROW_GET_RESULT(builder, arrow::MakeBuilder(field->type()));
      output_array_builders.emplace_back(std::move(builder));
    }

    std::vector<std::shared_ptr<arrow::Array>> input_data = batch->columns();
    int num_rows = batch->num_rows();
    int num_cols = input_data.size();

    for (int i = 0; i < num_rows; i++) {
      std::vector<std::shared_ptr<arrow::Scalar>> current_keys;
      current_keys.reserve(input_data.size());
      for (int j = 0; j < num_cols; j++) {
        std::shared_ptr<arrow::Scalar> scalar;
        PSI_ARROW_GET_RESULT(scalar, input_data[j]->GetScalar(i));
        current_keys.emplace_back(scalar);
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
          self_total_cnt += cnt;
          cnt = 1;
          first_appearance = idx;
        }
      }
      idx++;
    }

    std::vector<std::shared_ptr<arrow::Array>> output_arrays;
    output_arrays.reserve(output_fields.size());
    for (auto& builder : output_array_builders) {
      std::shared_ptr<arrow::Array> array;
      PSI_ARROW_GET_RESULT(array, builder->Finish());
      output_arrays.emplace_back(array);
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
    std::unique_ptr<arrow::ArrayBuilder> builder;
    PSI_ARROW_GET_RESULT(builder, arrow::MakeBuilder(field->type()));
    output_array_builders.emplace_back(std::move(builder));
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
  self_total_cnt += cnt;

  std::vector<std::shared_ptr<arrow::Array>> output_arrays;
  output_arrays.reserve(output_fields.size());
  for (auto& builder : output_array_builders) {
    std::shared_ptr<arrow::Array> array;
    PSI_ARROW_GET_RESULT(array, builder->Finish());
    output_arrays.emplace_back(array);
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

  config->self_total_cnt = self_total_cnt;
}

void SendSelfCnt(const std::shared_ptr<yacl::link::Context>& link_ctx,
                 AdvancedJoinConfig* config) {
  YACL_ENFORCE(link_ctx->WorldSize() == 2);
  YACL_ENFORCE(std::filesystem::exists(config->self_intersection_cnt_path),
               "Self intersection cnt file {} doesn't exist.",
               config->self_intersection_cnt_path);

  std::shared_ptr<arrow::io::ReadableFile> infile;
  PSI_ARROW_GET_RESULT(
      infile, arrow::io::ReadableFile::Open(config->self_intersection_cnt_path,
                                            arrow::default_memory_pool()));

  auto read_options = arrow::csv::ReadOptions::Defaults();
  auto parse_options = arrow::csv::ParseOptions::Defaults();
  auto convert_options = arrow::csv::ConvertOptions::Defaults();
  convert_options.include_columns =
      std::vector<std::string>{kAdvancedJoinKeyCount};

  arrow::io::IOContext io_context = arrow::io::default_io_context();

  std::shared_ptr<arrow::csv::StreamingReader> reader;
  PSI_ARROW_GET_RESULT(reader, arrow::csv::StreamingReader::Make(
                                   io_context, infile, read_options,
                                   parse_options, convert_options));

  std::shared_ptr<arrow::RecordBatch> batch;

  int64_t self_intersection_cnt = 0;

  while (true) {
    arrow::Status status = reader->ReadNext(&batch);

    if (!status.ok()) {
      YACL_THROW("Read csv error.");
    }

    if (batch == NULL) {
      // send an empty record to indicate the end of sending.
      link_ctx->SendAsync(link_ctx->NextRank(), "", kIntersectionLinkTag);

      config->self_intersection_cnt = self_intersection_cnt;
      config->self_difference_cnt =
          config->self_total_cnt - config->self_intersection_cnt;

      int64_t self_difference_cnt_to_sync = config->self_difference_cnt;
      if (config->type == v2::PsiConfig::ADVANCED_JOIN_TYPE_INNER_JOIN) {
        self_difference_cnt_to_sync = 0;
      }

      yacl::Buffer buffer(reinterpret_cast<char*>(&self_difference_cnt_to_sync),
                          sizeof(int64_t));

      link_ctx->SendAsync(link_ctx->NextRank(), buffer, kMetaLinkTag);

      break;
    } else {
      auto cnt_array =
          std::static_pointer_cast<arrow::Int64Array>(batch->column(0));
      arrow::Datum sum;
      PSI_ARROW_GET_RESULT(sum, arrow::compute::Sum(batch->column(0)));
      self_intersection_cnt += sum.scalar_as<arrow::Int64Scalar>().value;

      std::shared_ptr<arrow::Buffer> buffer = cnt_array->values();
      link_ctx->SendAsync(
          link_ctx->NextRank(),
          yacl::ByteContainerView(buffer->data(), buffer->size()),
          kIntersectionLinkTag);
    }
  }

  if (!infile->Close().ok()) {
    YACL_THROW("infile Close failed.");
  }
}

void RecvPeerCnt(const std::shared_ptr<yacl::link::Context>& link_ctx,
                 AdvancedJoinConfig* config) {
  YACL_ENFORCE(link_ctx->WorldSize() == 2);

  std::shared_ptr<arrow::io::FileOutputStream> outfile =
      io::GetArrowOutputStream(config->peer_intersection_cnt_path);
  auto write_options = arrow::csv::WriteOptions::Defaults();
  write_options.include_header = true;

  std::shared_ptr<arrow::Schema> schema =
      arrow::schema({arrow::field(kAdvancedJoinKeyCount, arrow::int64())});

  std::shared_ptr<arrow::ipc::RecordBatchWriter> writer;
  PSI_ARROW_GET_RESULT(
      writer, arrow::csv::MakeCSVWriter(outfile, schema, write_options));

  while (true) {
    yacl::Buffer buffer =
        link_ctx->Recv(link_ctx->NextRank(), kIntersectionLinkTag);

    int buffer_size = buffer.size();

    if (buffer_size == 0) {
      // Receiving empty record which indicates the end of peer sending.
      buffer = link_ctx->Recv(link_ctx->NextRank(), kMetaLinkTag);
      int64_t value;
      std::memcpy(&value, buffer.data(), buffer.size());
      config->peer_difference_cnt = value;
      break;
    }

    int64_t* buffer_data = buffer.data<int64_t>();

    std::shared_ptr<arrow::ArrayBuilder> builder;
    PSI_ARROW_GET_RESULT(builder, arrow::MakeBuilder(arrow::int64()));

    for (size_t i = 0; i < buffer_size / sizeof(int64_t); i++) {
      YACL_ENFORCE(
          builder->AppendScalar(arrow::Int64Scalar(buffer_data[i])).ok());
    }

    std::vector<std::shared_ptr<arrow::Array>> output_arrays;
    std::shared_ptr<arrow::Array> array;
    PSI_ARROW_GET_RESULT(array, builder->Finish());
    output_arrays.emplace_back(array);

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

void AdvancedJoinSync(const std::shared_ptr<yacl::link::Context>& link_ctx,
                      AdvancedJoinConfig* config) {
  YACL_ENFORCE(link_ctx->WorldSize() == 2);

  std::future<void> f_send_self_cnt =
      std::async([&] { SendSelfCnt(link_ctx, config); });
  std::future<void> f_recv_peer_cnt =
      std::async([&] { RecvPeerCnt(link_ctx, config); });

  try {
    f_send_self_cnt.get();
  } catch (...) {
    std::exception_ptr ep = std::current_exception();
    std::rethrow_exception(ep);
  }

  try {
    f_recv_peer_cnt.get();
  } catch (...) {
    std::exception_ptr ep = std::current_exception();
    std::rethrow_exception(ep);
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
    std::shared_ptr<arrow::Scalar> scalar;
    PSI_ARROW_GET_RESULT(scalar, (*batch)->GetColumnByName(col)->GetScalar(
                                     previous_read_batch_index));
    res.emplace_back(scalar);
  }

  return res;
}

void GenerateOutputForSingleRecord(
    const std::shared_ptr<arrow::ipc::RecordBatchWriter>& writer,
    const std::shared_ptr<arrow::ipc::RecordBatchWriter>& diff_writer,
    const std::shared_ptr<arrow::csv::StreamingReader>& sorted_input_reader,
    const std::vector<std::string>& key_col_names,
    const std::vector<std::shared_ptr<arrow::Scalar>>& target_keys,
    int64_t self_cnt, int64_t peer_cnt, v2::Role role,
    std::shared_ptr<arrow::RecordBatch>* sorted_input_batch,
    int64_t* read_batch_index, int64_t* write_diff_cnt, bool write_intersection,
    bool write_difference) {
  bool found = false;

  // collect all rows with the same keys.
  std::vector<std::vector<std::shared_ptr<arrow::Scalar>>> output_scalars;
  output_scalars.reserve(sorted_input_reader->schema()->num_fields());

  for (int i = 0; i < sorted_input_reader->schema()->num_fields(); i++) {
    output_scalars.emplace_back(std::vector<std::shared_ptr<arrow::Scalar>>());
  }

  std::vector<std::unique_ptr<arrow::ArrayBuilder>> differ_array_builders;
  differ_array_builders.reserve(sorted_input_reader->schema()->num_fields());

  for (int i = 0; i < sorted_input_reader->schema()->num_fields(); i++) {
    auto builder =
        arrow::MakeBuilder(sorted_input_reader->schema()->field(i)->type());
    YACL_ENFORCE(builder.ok(), "MakeBuilder: type: {} failed.",
                 sorted_input_reader->schema()->field(i)->type()->name());
    differ_array_builders.emplace_back(std::move(*builder));
  }

  // found target keys;
  while (!found) {
    if (*sorted_input_batch == NULL ||
        *read_batch_index >= (*sorted_input_batch)->num_rows()) {
      arrow::Status status = sorted_input_reader->ReadNext(sorted_input_batch);

      if (!status.ok()) {
        YACL_THROW("Read csv error.");
      }

      if (*sorted_input_batch == NULL) {
        YACL_THROW("sorted_input_reader: schema {} reach the end.",
                   sorted_input_reader->schema()->ToString());
      }

      SPDLOG_INFO("read batch: rows: {}", (*sorted_input_batch)->num_rows());

      *read_batch_index = 0;
    }

    for (; *read_batch_index < (*sorted_input_batch)->num_rows();
         *read_batch_index = *read_batch_index + 1) {
      std::vector<std::shared_ptr<arrow::Scalar>> current_keys;
      current_keys.reserve(key_col_names.size());
      for (const std::string& col_name : key_col_names) {
        std::shared_ptr<arrow::Scalar> key;
        PSI_ARROW_GET_RESULT(key, (*sorted_input_batch)
                                      ->GetColumnByName(col_name)
                                      ->GetScalar(*read_batch_index));
        current_keys.emplace_back(key);
        SPDLOG_DEBUG("index: {}, current_key: {}<{}>", *read_batch_index,
                     key->ToString(), key->type->ToString());
      }

      bool equal = true;
      for (size_t i = 0; i < key_col_names.size(); i++) {
        if (!current_keys[i]->Equals(target_keys[i])) {
          SPDLOG_DEBUG(
              "index {} : target_keys[{}<{}>] != current_keys[{}<{}>]", i,
              target_keys[i]->ToString(), target_keys[i]->type->ToString(),
              current_keys[i]->ToString(), current_keys[i]->type->ToString());
          equal = false;
          break;
        }
        SPDLOG_DEBUG("index {} : target_keys[{}] == current_keys[{}]", i,
                     target_keys[i]->ToString(), current_keys[i]->ToString());
      }

      if (equal) {
        found = true;
        break;
      } else {
        if (write_difference) {
          for (int i = 0; i < sorted_input_reader->schema()->num_fields();
               i++) {
            std::shared_ptr<arrow::Scalar> scalar;
            PSI_ARROW_GET_RESULT(
                scalar,
                (*sorted_input_batch)->column(i)->GetScalar(*read_batch_index));
            YACL_ENFORCE(differ_array_builders[i]->AppendScalar(*scalar).ok());
          }
        }
      }
    }
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
        std::shared_ptr<arrow::Scalar> key;
        PSI_ARROW_GET_RESULT(key, (*sorted_input_batch)
                                      ->GetColumnByName(col_name)
                                      ->GetScalar(*read_batch_index));
        current_keys.emplace_back(key);
      }

      bool equal = true;
      for (size_t i = 0; i < key_col_names.size(); i++) {
        if (!current_keys[i]->Equals(target_keys[i])) {
          equal = false;
          break;
        }
      }

      if (equal) {
        if (write_intersection) {
          for (int i = 0; i < sorted_input_reader->schema()->num_fields();
               i++) {
            std::shared_ptr<arrow::Scalar> scalar;
            PSI_ARROW_GET_RESULT(
                scalar,
                (*sorted_input_batch)->column(i)->GetScalar(*read_batch_index));
            output_scalars[i].emplace_back(scalar);
          }
        }
      } else {
        finished = true;
        break;
      }
    }
  }

  if (write_intersection) {
    YACL_ENFORCE_EQ(static_cast<int64_t>(output_scalars[0].size()), self_cnt,
                    "self_cnt doesn't match the actual sorted input.");

    // Write to output
    std::vector<std::unique_ptr<arrow::ArrayBuilder>> output_array_builders;
    output_array_builders.reserve(sorted_input_reader->schema()->num_fields());

    for (int i = 0; i < sorted_input_reader->schema()->num_fields(); i++) {
      std::unique_ptr<arrow::ArrayBuilder> builder;
      PSI_ARROW_GET_RESULT(
          builder,
          arrow::MakeBuilder(sorted_input_reader->schema()->field(i)->type()));
      output_array_builders.emplace_back(std::move(builder));
      SPDLOG_DEBUG("field: {}, name: {}, type:{}", i,
                   sorted_input_reader->schema()->field(i)->name(),
                   sorted_input_reader->schema()->field(i)->type()->ToString());
    }

    if (role == v2::ROLE_RECEIVER) {
      for (int i = 0; i < self_cnt; i++) {
        for (int j = 0; j < peer_cnt; j++) {
          for (int k = 0; k < sorted_input_reader->schema()->num_fields();
               k++) {
            YACL_ENFORCE(output_array_builders[k]
                             ->AppendScalar(*(output_scalars[k][i]))
                             .ok());
          }
        }
      }
    } else {
      for (int i = 0; i < peer_cnt; i++) {
        for (int j = 0; j < self_cnt; j++) {
          for (int k = 0; k < sorted_input_reader->schema()->num_fields();
               k++) {
            YACL_ENFORCE(output_array_builders[k]
                             ->AppendScalar(*(output_scalars[k][j]))
                             .ok(),
                         "AppendScalar failed. k: {}, j: {}, scalar_type: {}, "
                         "scalar: {}, builder: {}",
                         k, j, output_scalars[k][j]->type->ToString(),
                         output_scalars[k][j]->ToString(),
                         output_array_builders[k]->type()->ToString());
          }
        }
      }
    }

    std::vector<std::shared_ptr<arrow::Array>> output_arrays;
    output_arrays.reserve(sorted_input_reader->schema()->num_fields());

    for (auto& builder : output_array_builders) {
      std::shared_ptr<arrow::Array> array;
      PSI_ARROW_GET_RESULT(array, builder->Finish());
      output_arrays.emplace_back(array);
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

  // Write to difference
  if (write_difference) {
    std::vector<std::shared_ptr<arrow::Array>> differ_arrays;
    differ_arrays.reserve(sorted_input_reader->schema()->num_fields());

    for (auto& builder : differ_array_builders) {
      std::shared_ptr<arrow::Array> array;
      PSI_ARROW_GET_RESULT(array, builder->Finish());
      differ_arrays.emplace_back(array);
    }

    if (differ_arrays[0]->length() > 0) {
      std::shared_ptr<arrow::RecordBatch> differ_batch =
          arrow::RecordBatch::Make(sorted_input_reader->schema(),
                                   differ_arrays[0]->length(), differ_arrays);
      if (!diff_writer->WriteRecordBatch(*differ_batch).ok()) {
        YACL_THROW("diff_writer WriteRecordBatch failed.");
      }

      *write_diff_cnt = *write_diff_cnt + differ_batch->num_rows();
    }
  }
}

void AppendDifferenceToOutput(
    const AdvancedJoinConfig& config,
    const std::shared_ptr<arrow::ipc::RecordBatchWriter>& writer) {
  if (config.self_difference_cnt == 0) {
    return;
  }

  YACL_ENFORCE(std::filesystem::exists(config.difference_output_path),
               "Difference output file {} doesn't exist.",
               config.difference_output_path);

  arrow::io::IOContext io_context = arrow::io::default_io_context();
  std::shared_ptr<arrow::io::ReadableFile> infile;
  PSI_ARROW_GET_RESULT(
      infile, arrow::io::ReadableFile::Open(config.difference_output_path,
                                            arrow::default_memory_pool()));

  auto read_options = arrow::csv::ReadOptions::Defaults();
  auto parse_options = arrow::csv::ParseOptions::Defaults();
  auto convert_options = arrow::csv::ConvertOptions::Defaults();

  std::shared_ptr<arrow::csv::StreamingReader> reader;
  PSI_ARROW_GET_RESULT(reader, arrow::csv::StreamingReader::Make(
                                   io_context, infile, read_options,
                                   parse_options, convert_options));

  std::shared_ptr<arrow::RecordBatch> batch;
  while (true) {
    arrow::Status status = reader->ReadNext(&batch);

    if (!status.ok()) {
      YACL_THROW("Read csv error.");
    }

    if (batch == NULL) {
      break;
    }

    if (!writer->WriteRecordBatch(*batch).ok()) {
      YACL_THROW("writer WriteRecordBatch failed.");
    }
  }
}

void AppendAlignedNullToOutput(
    const AdvancedJoinConfig& config,
    const std::shared_ptr<arrow::Schema>& schema,
    const std::shared_ptr<arrow::ipc::RecordBatchWriter>& writer) {
  if (config.peer_difference_cnt == 0) {
    return;
  }

  std::vector<std::shared_ptr<arrow::Array>> output_arrays;
  output_arrays.reserve(schema->num_fields());
  for (int i = 0; i < schema->num_fields(); i++) {
    std::unique_ptr<arrow::ArrayBuilder> builder;
    PSI_ARROW_GET_RESULT(builder, arrow::MakeBuilder(schema->field(i)->type()));
    YACL_ENFORCE(builder->AppendNulls(config.peer_difference_cnt).ok());
    std::shared_ptr<arrow::Array> array;
    PSI_ARROW_GET_RESULT(array, builder->Finish());
    output_arrays.emplace_back(array);
  }

  std::shared_ptr<arrow::RecordBatch> output_batch = arrow::RecordBatch::Make(
      schema, output_arrays[0]->length(), output_arrays);
  if (!writer->WriteRecordBatch(*output_batch).ok()) {
    YACL_THROW("writer WriteRecordBatch failed.");
  }
}

void AdvancedJoinGenerateResult(const AdvancedJoinConfig& config) {
  YACL_ENFORCE(std::filesystem::exists(config.sorted_input_path),
               "Sorted input file {} doesn't exist.", config.sorted_input_path);

  YACL_ENFORCE(std::filesystem::exists(config.self_intersection_cnt_path),
               "Sorted input file {} doesn't exist.",
               config.self_intersection_cnt_path);

  YACL_ENFORCE(std::filesystem::exists(config.peer_intersection_cnt_path),
               "Sorted input file {} doesn't exist.",
               config.peer_intersection_cnt_path);

  SPDLOG_INFO("config.sorted_input_path = {}", config.sorted_input_path);
  SPDLOG_INFO("config.self_intersection_cnt_path = {}",
              config.self_intersection_cnt_path);
  SPDLOG_INFO("config.peer_intersection_cnt_path = {}",
              config.peer_intersection_cnt_path);
  SPDLOG_INFO("config.output_path = {}", config.output_path);

  std::shared_ptr<arrow::io::ReadableFile> self_intersection_cnt_infile;
  PSI_ARROW_GET_RESULT(
      self_intersection_cnt_infile,
      arrow::io::ReadableFile::Open(config.self_intersection_cnt_path,
                                    arrow::default_memory_pool()));

  std::shared_ptr<arrow::io::ReadableFile> peer_intersection_cnt_infile;
  PSI_ARROW_GET_RESULT(
      peer_intersection_cnt_infile,
      arrow::io::ReadableFile::Open(config.peer_intersection_cnt_path,
                                    arrow::default_memory_pool()));

  auto read_options = arrow::csv::ReadOptions::Defaults();
  auto parse_options = arrow::csv::ParseOptions::Defaults();
  auto self_intersection_cnt_convert_options =
      arrow::csv::ConvertOptions::Defaults();

  std::vector<std::string> keys = config.keys;
  keys.emplace_back(kAdvancedJoinKeyCount);
  self_intersection_cnt_convert_options.column_types[kAdvancedJoinKeyCount] =
      arrow::int64();
  self_intersection_cnt_convert_options.include_columns = keys;
  for (const auto& key : config.keys) {
    self_intersection_cnt_convert_options.column_types[key] = arrow::utf8();
  }

  std::shared_ptr<arrow::csv::StreamingReader> self_intersection_cnt_reader;
  PSI_ARROW_GET_RESULT(
      self_intersection_cnt_reader,
      arrow::csv::StreamingReader::Make(
          arrow::io::default_io_context(), self_intersection_cnt_infile,
          read_options, parse_options, self_intersection_cnt_convert_options));

  auto peer_intersection_cnt_convert_options =
      arrow::csv::ConvertOptions::Defaults();
  peer_intersection_cnt_convert_options.include_columns =
      std::vector<std::string>{kAdvancedJoinKeyCount};
  peer_intersection_cnt_convert_options.column_types[kAdvancedJoinKeyCount] =
      arrow::int64();
  std::shared_ptr<arrow::csv::StreamingReader> peer_intersection_cnt_reader;
  PSI_ARROW_GET_RESULT(
      peer_intersection_cnt_reader,
      arrow::csv::StreamingReader::Make(
          arrow::io::default_io_context(), peer_intersection_cnt_infile,
          read_options, parse_options, peer_intersection_cnt_convert_options));

  auto sorted_input_convert_options = arrow::csv::ConvertOptions::Defaults();
  {
    std::shared_ptr<arrow::io::ReadableFile> sorted_input_infile;
    PSI_ARROW_GET_RESULT(
        sorted_input_infile,
        arrow::io::ReadableFile::Open(config.sorted_input_path,
                                      arrow::default_memory_pool()));
    std::shared_ptr<arrow::csv::StreamingReader> sorted_input_reader;
    PSI_ARROW_GET_RESULT(
        sorted_input_reader,
        arrow::csv::StreamingReader::Make(
            arrow::io::default_io_context(), sorted_input_infile, read_options,
            parse_options, sorted_input_convert_options));

    for (const auto& field : sorted_input_reader->schema()->fields()) {
      sorted_input_convert_options.column_types[field->name()] = arrow::utf8();
    }
  }
  std::shared_ptr<arrow::io::ReadableFile> sorted_input_infile;
  PSI_ARROW_GET_RESULT(sorted_input_infile, arrow::io::ReadableFile::Open(
                                                config.sorted_input_path,
                                                arrow::default_memory_pool()));
  std::shared_ptr<arrow::csv::StreamingReader> sorted_input_reader;
  PSI_ARROW_GET_RESULT(
      sorted_input_reader,
      arrow::csv::StreamingReader::Make(
          arrow::io::default_io_context(), sorted_input_infile, read_options,
          parse_options, sorted_input_convert_options));

  std::shared_ptr<arrow::RecordBatch> self_intersection_cnt_batch;
  std::shared_ptr<arrow::RecordBatch> peer_intersection_cnt_batch;
  std::shared_ptr<arrow::RecordBatch> sorted_input_batch;

  std::shared_ptr<arrow::io::FileOutputStream> outfile =
      io::GetArrowOutputStream(config.output_path);
  auto write_options = arrow::csv::WriteOptions::Defaults();
  write_options.include_header = true;
  write_options.quoting_style = arrow::csv::QuotingStyle::None;
  write_options.null_string = "NA";

  std::shared_ptr<arrow::ipc::RecordBatchWriter> writer;
  PSI_ARROW_GET_RESULT(
      writer, arrow::csv::MakeCSVWriter(outfile, sorted_input_reader->schema(),
                                        write_options));

  std::shared_ptr<arrow::io::FileOutputStream> diff_outfile =
      io::GetArrowOutputStream(config.difference_output_path);
  std::shared_ptr<arrow::ipc::RecordBatchWriter> diff_writer;
  PSI_ARROW_GET_RESULT(
      diff_writer,
      arrow::csv::MakeCSVWriter(diff_outfile, sorted_input_reader->schema(),
                                write_options));

  int64_t input_batch_index = 0;
  int64_t peer_cnt_batch_index = 0;
  int64_t write_diff_cnt = 0;

  bool write_intersection =
      config.type != v2::PsiConfig::ADVANCED_JOIN_TYPE_DIFFERENCE;
  bool write_difference =
      config.type != v2::PsiConfig::ADVANCED_JOIN_TYPE_INNER_JOIN;

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
        self_intersection_cnt_data[config.keys.size()]);

    for (int i = 0; i < self_intersection_cnt_batch->num_rows(); i++) {
      std::vector<std::shared_ptr<arrow::Scalar>> current_keys;
      current_keys.reserve(config.keys.size());
      for (size_t j = 0; j < config.keys.size(); j++) {
        std::shared_ptr<arrow::Scalar> scalar;
        PSI_ARROW_GET_RESULT(scalar,
                             self_intersection_cnt_data[j]->GetScalar(i));
        current_keys.emplace_back(scalar);
      }

      int64_t current_self_count = self_count_array->Value(i);

      std::vector<std::shared_ptr<arrow::Scalar>> record =
          ReadNextRecord(peer_intersection_cnt_reader,
                         &peer_intersection_cnt_batch, &peer_cnt_batch_index,
                         std::vector<std::string>{kAdvancedJoinKeyCount});

      int64_t peer_count =
          std::dynamic_pointer_cast<arrow::Int64Scalar>(record[0])->value;

      YACL_ENFORCE_GE(peer_count, 1);

      GenerateOutputForSingleRecord(
          writer, diff_writer, sorted_input_reader, config.keys, current_keys,
          current_self_count, peer_count, config.role, &sorted_input_batch,
          &input_batch_index, &write_diff_cnt, write_intersection,
          write_difference);
    }
  }

  YACL_ENFORCE(ReadNextRecord(peer_intersection_cnt_reader,
                              &peer_intersection_cnt_batch,
                              &peer_cnt_batch_index,
                              std::vector<std::string>{kAdvancedJoinKeyCount})
                   .empty());

  while (true) {
    if (sorted_input_batch != NULL && input_batch_index >= 0 &&
        input_batch_index < sorted_input_batch->num_rows()) {
      std::shared_ptr<arrow::RecordBatch> slice_batch =
          sorted_input_batch->Slice(input_batch_index);
      write_diff_cnt += slice_batch->num_rows();
      if (!diff_writer->WriteRecordBatch(*slice_batch).ok()) {
        YACL_THROW("diff_writer WriteRecordBatch failed.");
      }
      input_batch_index = -1;
    } else {
      arrow::Status status = sorted_input_reader->ReadNext(&sorted_input_batch);

      if (!status.ok()) {
        YACL_THROW("Read csv error.");
      }

      if (sorted_input_batch == NULL) {
        break;
      }

      input_batch_index = -1;
      write_diff_cnt += sorted_input_batch->num_rows();
      if (!diff_writer->WriteRecordBatch(*sorted_input_batch).ok()) {
        YACL_THROW("diff_writer WriteRecordBatch failed.");
      }
    }
  }

  if (!diff_outfile->Close().ok()) {
    YACL_THROW("diff_outfile Close failed.");
  }

  // Handle left difference.
  if (config.type == v2::PsiConfig::ADVANCED_JOIN_TYPE_LEFT_JOIN ||
      config.type == v2::PsiConfig::ADVANCED_JOIN_TYPE_FULL_JOIN ||
      config.type == v2::PsiConfig::ADVANCED_JOIN_TYPE_DIFFERENCE) {
    if (config.role == config.left_side) {
      AppendDifferenceToOutput(config, writer);
    } else {
      AppendAlignedNullToOutput(config, sorted_input_reader->schema(), writer);
    }
  }

  // Handle right difference.
  if (config.type == v2::PsiConfig::ADVANCED_JOIN_TYPE_RIGHT_JOIN ||
      config.type == v2::PsiConfig::ADVANCED_JOIN_TYPE_FULL_JOIN ||
      config.type == v2::PsiConfig::ADVANCED_JOIN_TYPE_DIFFERENCE) {
    if (config.role == config.left_side) {
      AppendAlignedNullToOutput(config, sorted_input_reader->schema(), writer);
    } else {
      AppendDifferenceToOutput(config, writer);
    }
  }

  if (write_difference) {
    YACL_ENFORCE_EQ(
        write_diff_cnt, config.self_difference_cnt,
        "Actual difference cnt doesn't match the record in config.");
  }

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

}  // namespace psi
