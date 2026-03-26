#include "psi/algorithm/ypir/entry.h"

#include <filesystem>
#include <fstream>
#include <future>
#include <string>
#include <vector>

#include "absl/strings/escaping.h"
#include "gtest/gtest.h"
#include "yacl/link/test_util.h"

#include "psi/algorithm/ypir/params.h"

namespace psi::ypir {
namespace {

std::string HexByte(uint64_t value) {
  std::vector<uint8_t> bytes = {static_cast<uint8_t>(value)};
  return absl::BytesToHexString(absl::string_view(
      reinterpret_cast<const char*>(bytes.data()), bytes.size()));
}

std::string HexUint16(uint64_t value) {
  const uint16_t narrowed = static_cast<uint16_t>(value);
  return absl::BytesToHexString(absl::string_view(
      reinterpret_cast<const char*>(&narrowed), sizeof(narrowed)));
}

void WriteLines(const std::filesystem::path& path,
                const std::vector<std::string>& lines) {
  std::ofstream output(path, std::ios::out | std::ios::trunc);
  ASSERT_TRUE(output.is_open());
  for (const auto& line : lines) {
    output << line << '\n';
  }
}

std::vector<std::string> ReadLines(const std::filesystem::path& path) {
  std::ifstream input(path);
  EXPECT_TRUE(input.is_open());
  std::vector<std::string> lines;
  std::string line;
  while (std::getline(input, line)) {
    if (!line.empty()) {
      lines.push_back(line);
    }
  }
  return lines;
}

TEST(YpirEntryTest, SimplepirOnlineFlowWorksWithHexFiles) {
  const auto params = CreateSmallTestParamsSimplePIR();
  const auto tmp_dir =
      std::filesystem::temp_directory_path() / "ypir_entry_simplepir";
  std::filesystem::create_directories(tmp_dir);

  const auto db_path = tmp_dir / "db.hex";
  const auto query_path = tmp_dir / "query.txt";
  const auto output_path = tmp_dir / "result.hex";

  std::vector<std::string> db_lines;
  db_lines.reserve(params.NumItems());
  for (uint64_t raw_idx = 0; raw_idx < params.NumItems(); ++raw_idx) {
    const uint64_t row = raw_idx / params.db_cols;
    const uint64_t col = raw_idx % params.db_cols;
    db_lines.push_back(
        HexUint16((row * 17 + col * 3) % params.spiral_params.PtModulus()));
  }
  WriteLines(db_path, db_lines);
  WriteLines(query_path, {"0", "123456", "1048575"});

  auto lctxs = yacl::link::test::SetupWorld(2);
  YpirSenderOptions sender_options;
  sender_options.mode = YpirMode::kSimplepir;
  sender_options.db_rows = params.db_rows;
  sender_options.db_cols = params.db_cols;
  sender_options.item_size_bits = 16;
  sender_options.db_file = db_path;

  YpirReceiverOptions receiver_options;
  receiver_options.mode = YpirMode::kSimplepir;
  receiver_options.db_rows = params.db_rows;
  receiver_options.db_cols = params.db_cols;
  receiver_options.item_size_bits = 16;
  receiver_options.query_file = query_path;
  receiver_options.output_file = output_path;

  auto sender = std::async(std::launch::async, [&] {
    return SenderOnline(sender_options, lctxs[0]);
  });
  auto receiver = std::async(std::launch::async, [&] {
    return ReceiverOnline(receiver_options, lctxs[1]);
  });

  EXPECT_EQ(sender.get(), 0);
  EXPECT_EQ(receiver.get(), 0);

  EXPECT_EQ(
      ReadLines(output_path),
      (std::vector<std::string>{HexUint16(0),
                                HexUint16((120ULL * 17 + 576ULL * 3) %
                                          params.spiral_params.PtModulus()),
                                HexUint16((1023ULL * 17 + 1023ULL * 3) %
                                          params.spiral_params.PtModulus())}));

  std::filesystem::remove_all(tmp_dir);
}

TEST(YpirEntryTest, DoublepirOnlineFlowWorksWithHexFiles) {
  const auto params = CreateSmallTestParamsDoublePIR();
  const auto tmp_dir =
      std::filesystem::temp_directory_path() / "ypir_entry_doublepir";
  std::filesystem::create_directories(tmp_dir);

  const auto db_path = tmp_dir / "db.hex";
  const auto query_path = tmp_dir / "query.txt";
  const auto output_path = tmp_dir / "result.hex";

  std::vector<std::string> db_lines;
  db_lines.reserve(params.NumItems());
  for (uint64_t raw_idx = 0; raw_idx < params.NumItems(); ++raw_idx) {
    const uint64_t row = raw_idx / params.db_cols;
    const uint64_t col = raw_idx % params.db_cols;
    db_lines.push_back(HexByte((row + col) % 251));
  }
  WriteLines(db_path, db_lines);
  WriteLines(query_path, {"0", "113886", "1048575"});

  auto lctxs = yacl::link::test::SetupWorld(2);
  YpirSenderOptions sender_options;
  sender_options.mode = YpirMode::kDoublepir;
  sender_options.db_rows = params.db_rows;
  sender_options.db_cols = params.db_cols;
  sender_options.item_size_bits = 8;
  sender_options.db_file = db_path;

  YpirReceiverOptions receiver_options;
  receiver_options.mode = YpirMode::kDoublepir;
  receiver_options.db_rows = params.db_rows;
  receiver_options.db_cols = params.db_cols;
  receiver_options.item_size_bits = 8;
  receiver_options.query_file = query_path;
  receiver_options.output_file = output_path;

  auto sender = std::async(std::launch::async, [&] {
    return SenderOnline(sender_options, lctxs[0]);
  });
  auto receiver = std::async(std::launch::async, [&] {
    return ReceiverOnline(receiver_options, lctxs[1]);
  });

  EXPECT_EQ(sender.get(), 0);
  EXPECT_EQ(receiver.get(), 0);

  EXPECT_EQ(
      ReadLines(output_path),
      (std::vector<std::string>{HexByte(0), HexByte((111ULL + 222ULL) % 251),
                                HexByte((1023ULL + 1023ULL) % 251)}));

  std::filesystem::remove_all(tmp_dir);
}

}  // namespace
}  // namespace psi::ypir
