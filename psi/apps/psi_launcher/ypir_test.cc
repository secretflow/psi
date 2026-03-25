#include "psi/apps/psi_launcher/ypir_launch.h"

#include <filesystem>
#include <fstream>
#include <future>
#include <string>
#include <vector>

#include "absl/strings/escaping.h"
#include "gtest/gtest.h"
#include "yacl/link/test_util.h"

namespace psi {
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

TEST(YpirLauncherTest, RunPirSimplepirUsesLauncherEntry) {
  const auto tmp_dir =
      std::filesystem::temp_directory_path() / "ypir_launcher_simplepir";
  std::filesystem::create_directories(tmp_dir);
  const auto db_path = tmp_dir / "db.hex";
  const auto query_path = tmp_dir / "query.txt";
  const auto output_path = tmp_dir / "result.hex";

  std::vector<std::string> db_lines;
  db_lines.reserve(1ULL << 20);
  for (uint64_t raw_idx = 0; raw_idx < (1ULL << 20); ++raw_idx) {
    const uint64_t row = raw_idx / (1ULL << 10);
    const uint64_t col = raw_idx % (1ULL << 10);
    db_lines.push_back(HexUint16((row * 17 + col * 3) % (1ULL << 14)));
  }
  WriteLines(db_path, db_lines);
  WriteLines(query_path, {"0", "123456", "1048575"});

  YpirSenderConfig sender_config;
  sender_config.set_mode(YPIR_PROTOCOL_MODE_SIMPLEPIR);
  sender_config.set_db_rows(1ULL << 10);
  sender_config.set_db_cols(1ULL << 10);
  sender_config.set_item_size_bits(16);
  sender_config.set_db_file(db_path);

  YpirReceiverConfig receiver_config;
  receiver_config.set_mode(YPIR_PROTOCOL_MODE_SIMPLEPIR);
  receiver_config.set_db_rows(1ULL << 10);
  receiver_config.set_db_cols(1ULL << 10);
  receiver_config.set_item_size_bits(16);
  receiver_config.set_query_file(query_path);
  receiver_config.set_output_file(output_path);

  auto lctxs = yacl::link::test::SetupWorld(2);
  auto sender = std::async(std::launch::async, [&] {
    return RunPir(sender_config, lctxs[0]);
  });
  auto receiver = std::async(std::launch::async, [&] {
    return RunPir(receiver_config, lctxs[1]);
  });

  EXPECT_EQ(sender.get().match_cnt(), 0);
  EXPECT_EQ(receiver.get().match_cnt(), 0);
  EXPECT_EQ(ReadLines(output_path),
            (std::vector<std::string>{HexUint16(0), HexUint16(3768),
                                      HexUint16(4076)}));

  std::filesystem::remove_all(tmp_dir);
}

TEST(YpirLauncherTest, RunPirDoublepirUsesLauncherEntry) {
  const auto tmp_dir =
      std::filesystem::temp_directory_path() / "ypir_launcher_doublepir";
  std::filesystem::create_directories(tmp_dir);
  const auto db_path = tmp_dir / "db.hex";
  const auto query_path = tmp_dir / "query.txt";
  const auto output_path = tmp_dir / "result.hex";

  std::vector<std::string> db_lines;
  db_lines.reserve(1ULL << 20);
  for (uint64_t raw_idx = 0; raw_idx < (1ULL << 20); ++raw_idx) {
    const uint64_t row = raw_idx / (1ULL << 10);
    const uint64_t col = raw_idx % (1ULL << 10);
    db_lines.push_back(HexByte((row + col) % 251));
  }
  WriteLines(db_path, db_lines);
  WriteLines(query_path, {"0", "113886", "1048575"});

  YpirSenderConfig sender_config;
  sender_config.set_mode(YPIR_PROTOCOL_MODE_DOUBLEPIR);
  sender_config.set_db_rows(1ULL << 10);
  sender_config.set_db_cols(1ULL << 10);
  sender_config.set_item_size_bits(8);
  sender_config.set_db_file(db_path);

  YpirReceiverConfig receiver_config;
  receiver_config.set_mode(YPIR_PROTOCOL_MODE_DOUBLEPIR);
  receiver_config.set_db_rows(1ULL << 10);
  receiver_config.set_db_cols(1ULL << 10);
  receiver_config.set_item_size_bits(8);
  receiver_config.set_query_file(query_path);
  receiver_config.set_output_file(output_path);

  auto lctxs = yacl::link::test::SetupWorld(2);
  auto sender = std::async(std::launch::async, [&] {
    return RunPir(sender_config, lctxs[0]);
  });
  auto receiver = std::async(std::launch::async, [&] {
    return RunPir(receiver_config, lctxs[1]);
  });

  EXPECT_EQ(sender.get().match_cnt(), 0);
  EXPECT_EQ(receiver.get().match_cnt(), 0);
  EXPECT_EQ(ReadLines(output_path),
            (std::vector<std::string>{HexByte(0), HexByte(82), HexByte(38)}));

  std::filesystem::remove_all(tmp_dir);
}

}  // namespace
}  // namespace psi
