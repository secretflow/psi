#include "psi/algorithm/ypir/entry.h"

#include <cctype>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/strings/escaping.h"
#include "fmt/format.h"
#include "spdlog/spdlog.h"
#include "yacl/base/byte_container_view.h"
#include "yacl/base/exception.h"

#include "psi/algorithm/pir_interface/pir_db.h"
#include "psi/algorithm/ypir/client.h"
#include "psi/algorithm/ypir/params.h"
#include "psi/algorithm/ypir/server.h"

namespace psi::ypir {
namespace {

constexpr char kQueryCountTag[] = "ypir/query_count";

std::string TrimAscii(std::string_view text) {
  size_t begin = 0;
  size_t end = text.size();
  while (begin < end &&
         std::isspace(static_cast<unsigned char>(text[begin])) != 0) {
    ++begin;
  }
  while (end > begin &&
         std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
    --end;
  }
  return std::string(text.substr(begin, end - begin));
}

uint8_t ParseHexNibble(char ch) {
  if (ch >= '0' && ch <= '9') {
    return static_cast<uint8_t>(ch - '0');
  }
  if (ch >= 'a' && ch <= 'f') {
    return static_cast<uint8_t>(10 + ch - 'a');
  }
  if (ch >= 'A' && ch <= 'F') {
    return static_cast<uint8_t>(10 + ch - 'A');
  }
  YACL_THROW("invalid hex character: {}", ch);
}

std::vector<uint8_t> ParseHexBytes(std::string text, size_t value_bytes) {
  if (text.size() >= 2 && text[0] == '0' &&
      (text[1] == 'x' || text[1] == 'X')) {
    text = text.substr(2);
  }
  YACL_ENFORCE_EQ(text.size(), value_bytes * 2,
                  "hex value width mismatch, expect {} bytes", value_bytes);
  std::vector<uint8_t> out(value_bytes, 0);
  for (size_t i = 0; i < value_bytes; ++i) {
    out[i] = static_cast<uint8_t>((ParseHexNibble(text[2 * i]) << 4) |
                                  ParseHexNibble(text[2 * i + 1]));
  }
  return out;
}

std::vector<std::string> ReadLines(const std::string& path) {
  std::ifstream input(path);
  YACL_ENFORCE(input.is_open(), "failed to open file: {}", path);

  std::vector<std::string> lines;
  std::string line;
  while (std::getline(input, line)) {
    auto trimmed = TrimAscii(line);
    if (!trimmed.empty()) {
      lines.push_back(std::move(trimmed));
    }
  }
  return lines;
}

void WriteLines(const std::string& path, const std::vector<std::string>& lines) {
  std::ofstream output(path, std::ios::out | std::ios::trunc);
  YACL_ENFORCE(output.is_open(), "failed to open output file: {}", path);
  for (const auto& line : lines) {
    output << line << '\n';
  }
}

template <typename T>
yacl::Buffer SerializeScalar(T value) {
  yacl::Buffer out(sizeof(T));
  std::memcpy(out.data(), &value, sizeof(T));
  return out;
}

template <typename T>
T DeserializeScalar(const yacl::ByteContainerView& buffer) {
  YACL_ENFORCE_EQ(buffer.size(), sizeof(T));
  T out = 0;
  std::memcpy(&out, buffer.data(), sizeof(T));
  return out;
}

std::string QueryTag(uint64_t idx) { return fmt::format("ypir/query/{}", idx); }

std::string ResponseTag(uint64_t idx) {
  return fmt::format("ypir/response/{}", idx);
}

YpirParameters BuildParameters(YpirMode mode, uint64_t db_rows, uint64_t db_cols,
                               uint64_t item_size_bits) {
  YACL_ENFORCE_GT(db_rows, 0U);
  YACL_ENFORCE_GT(db_cols, 0U);
  YACL_ENFORCE_GT(item_size_bits, 0U);

  if (mode == YpirMode::kSimplepir) {
    return CreateParamsForShapeSimplePIR(db_rows, db_cols, item_size_bits);
  }
  return CreateParamsForShapeDoublePIR(db_rows, db_cols, item_size_bits);
}

std::vector<std::vector<uint8_t>> LoadDatabaseRows(const std::string& db_file,
                                                   size_t value_bytes,
                                                   uint64_t num_items) {
  const auto lines = ReadLines(db_file);
  YACL_ENFORCE_EQ(lines.size(), num_items,
                  "db_file must contain exactly {} values", num_items);

  std::vector<std::vector<uint8_t>> rows;
  rows.reserve(lines.size());
  for (const auto& line : lines) {
    rows.push_back(ParseHexBytes(line, value_bytes));
  }
  return rows;
}

std::vector<uint64_t> LoadQueryIndices(const std::string& query_file,
                                       uint64_t num_items) {
  const auto lines = ReadLines(query_file);
  std::vector<uint64_t> indices;
  indices.reserve(lines.size());
  for (const auto& line : lines) {
    uint64_t raw_idx = std::stoull(line);
    YACL_ENFORCE_LT(raw_idx, num_items, "query index out of range");
    indices.push_back(raw_idx);
  }
  return indices;
}

std::vector<std::string> EncodeOutputLines(
    const std::vector<std::vector<uint8_t>>& values) {
  std::vector<std::string> out;
  out.reserve(values.size());
  for (const auto& value : values) {
    out.push_back(
        absl::BytesToHexString(absl::string_view(
            reinterpret_cast<const char*>(value.data()), value.size())));
  }
  return out;
}

template <typename T>
int RunSenderWithServer(const YpirParameters& params,
                        const YpirSenderOptions& options,
                        std::shared_ptr<yacl::link::Context> lctx) {
  YpirServer<T> server(params);
  server.GenerateFromRawData(psi::pir::RawDatabase(
      LoadDatabaseRows(options.db_file, params.value_bytes, params.NumItems())));

  lctx->ConnectToMesh();
  const uint64_t query_count =
      DeserializeScalar<uint64_t>(lctx->Recv(lctx->NextRank(), kQueryCountTag));
  for (uint64_t idx = 0; idx < query_count; ++idx) {
    auto query = lctx->Recv(lctx->NextRank(), QueryTag(idx));
    auto response = server.Response(query, yacl::Buffer());
    lctx->Send(lctx->NextRank(), response, ResponseTag(idx));
  }
  return 0;
}

}  // namespace

int SenderOnline(const YpirSenderOptions& options,
                 std::shared_ptr<yacl::link::Context> lctx) {
  YACL_ENFORCE(lctx != nullptr, "link context is required for YPIR sender");
  const auto params = BuildParameters(options.mode, options.db_rows,
                                      options.db_cols, options.item_size_bits);

  SPDLOG_INFO("Starting YPIR sender, mode={}, db_rows={}, db_cols={}",
              params.mode == YpirMode::kSimplepir ? "simplepir" : "doublepir",
              params.db_rows, params.db_cols);

  if (params.mode == YpirMode::kSimplepir) {
    YACL_ENFORCE_LE(params.value_bytes, sizeof(uint16_t),
                    "SimplePIR launcher currently supports up to 16-bit values");
    if (params.value_bytes == 1) {
      return RunSenderWithServer<uint8_t>(params, options, std::move(lctx));
    }
    return RunSenderWithServer<uint16_t>(params, options, std::move(lctx));
  }

  YACL_ENFORCE_EQ(params.value_bytes, 1U,
                  "DoublePIR launcher currently supports 8-bit values");
  return RunSenderWithServer<uint8_t>(params, options, std::move(lctx));
}

int ReceiverOnline(const YpirReceiverOptions& options,
                   std::shared_ptr<yacl::link::Context> lctx) {
  YACL_ENFORCE(lctx != nullptr, "link context is required for YPIR receiver");
  const auto params = BuildParameters(options.mode, options.db_rows,
                                      options.db_cols, options.item_size_bits);
  const auto query_indices = LoadQueryIndices(options.query_file, params.NumItems());

  SPDLOG_INFO("Starting YPIR receiver, mode={}, query_count={}",
              params.mode == YpirMode::kSimplepir ? "simplepir" : "doublepir",
              query_indices.size());

  YpirClient client(params);
  lctx->ConnectToMesh();
  lctx->Send(lctx->NextRank(), SerializeScalar<uint64_t>(query_indices.size()),
             kQueryCountTag);

  std::vector<std::vector<uint8_t>> decoded_values;
  decoded_values.reserve(query_indices.size());
  for (size_t idx = 0; idx < query_indices.size(); ++idx) {
    const auto raw_idx = query_indices[idx];
    auto query = client.GenerateIndexQuery(raw_idx);
    lctx->Send(lctx->NextRank(), query, QueryTag(idx));
    auto response = lctx->Recv(lctx->NextRank(), ResponseTag(idx));
    decoded_values.push_back(client.DecodeIndexResponse(response, raw_idx));
  }

  WriteLines(options.output_file, EncodeOutputLines(decoded_values));
  return 0;
}

}  // namespace psi::ypir
