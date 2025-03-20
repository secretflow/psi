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

#include "psi/algorithm/dkpir/common.h"

#include <fstream>
#include <sstream>

#include "yacl/crypto/rand/rand.h"

#include "psi/wrapper/apsi/utils/sender_db.h"

#include "psi/algorithm/dkpir/secret_key.pb.h"

namespace psi::dkpir {

struct Colors {
  static const std::string Red;
  static const std::string Green;
  static const std::string RedBold;
  static const std::string GreenBold;
  static const std::string Reset;
};

const std::string Colors::Red = "\033[31m";
const std::string Colors::Green = "\033[32m";
const std::string Colors::RedBold = "\033[1;31m";
const std::string Colors::GreenBold = "\033[1;32m";
const std::string Colors::Reset = "\033[0m";

yacl::math::MPInt ComputePoly(const std::vector<uint64_t> &poly,
                              const uint64_t &data) {
  YACL_ENFORCE(poly.size() == 2, "This is a linear function.");
  yacl::math::MPInt a(poly[0]);
  yacl::math::MPInt b(poly[1]);
  yacl::math::MPInt x(data);

  return a * x + b;
}

yacl::math::MPInt ComputePoly(const std::vector<uint64_t> &poly,
                              const uint64_t &data1, const uint64_t &data2) {
  YACL_ENFORCE(poly.size() == 2, "This is a linear function.");
  yacl::math::MPInt a(poly[0]);
  yacl::math::MPInt b(poly[1]);
  yacl::math::MPInt x(data1);
  yacl::math::MPInt y(data2);

  return a * x + b * y;
}

void Save(const std::vector<uint64_t> &poly,
          const heu::lib::algorithms::elgamal::SecretKey &secret_key,
          std::ostream &out) {
  YACL_ENFORCE(poly.size() == 2, "This should be a linear function.");
  psi::dkpir::SecretKeyProto secret_key_proto;

  secret_key_proto.add_polynomial(poly[0]);
  secret_key_proto.add_polynomial(poly[1]);

  yacl::Buffer buffer = secret_key.GetX().Serialize();
  std::string str(buffer);
  secret_key_proto.set_secret_key(str);

  if (!secret_key_proto.SerializeToOstream(&out)) {
    YACL_THROW("Failed to serialize SecretKeyProto");
  }
}

void Load(std::vector<uint64_t> &poly, yacl::math::MPInt &x, std::istream &in) {
  psi::dkpir::SecretKeyProto secret_key_proto;

  if (!secret_key_proto.ParsePartialFromIstream(&in)) {
    SPDLOG_ERROR("Failed to load secret_key");
    YACL_THROW("failed to load secret_key");
  }

  YACL_ENFORCE(secret_key_proto.polynomial_size() == 2 && poly.size() == 2,
               "This should be a linear function.");
  poly[0] = secret_key_proto.polynomial(0);
  poly[1] = secret_key_proto.polynomial(1);

  std::string str = secret_key_proto.secret_key();
  x.Deserialize(str);
}

void PrintIntersectionResults(
    const std::vector<std::string> &orig_items,
    const std::vector<::apsi::Item> &items,
    const std::vector<::apsi::receiver::MatchRecord> &intersection,
    const uint128_t &shuffle_seed, const uint64_t &shuffle_counter,
    const std::string &out_file, bool append_to_outfile) {
  if (orig_items.size() != items.size()) {
    throw std::invalid_argument("orig_items must have same size as items");
  }

  yacl::crypto::YaclReplayUrbg<uint32_t> gen(shuffle_seed, shuffle_counter);
  std::vector<uint64_t> shuffle_indexes(orig_items.size());
  for (uint64_t i = 0; i < orig_items.size(); ++i) {
    shuffle_indexes[i] = i;
  }
  std::shuffle(shuffle_indexes.begin(), shuffle_indexes.end(), gen);

  std::stringstream csv_output;
  std::string csv_header = "key,value";
  int match_cnt = 0;
  for (size_t i = 0; i < orig_items.size(); i++) {
    std::stringstream msg;
    if (intersection[shuffle_indexes[i]].found) {
      match_cnt++;
      // msg << Colors::GreenBold << orig_items[i] << Colors::Reset << "(FOUND)
      // ";
      csv_output << orig_items[i];
      if (intersection[shuffle_indexes[i]].label) {
        // msg << ": ";
        // msg << Colors::GreenBold <<
        // intersection[shuffle_indexes[i]].label.to_string()
        //     << Colors::Reset;
        csv_output << "," << intersection[shuffle_indexes[i]].label.to_string();
      }
      csv_output << std::endl;
      // APSI_LOG_INFO(msg.str());
    } else {
      // msg << Colors::RedBold << orig_items[i] << Colors::Reset << " (NOT
      // FOUND)"; APSI_LOG_INFO(msg.str());
    }
  }

  if (!out_file.empty()) {
    if (append_to_outfile) {
      std::ofstream ofs(out_file, std::ios_base::app);
      ofs << csv_output.str();
    } else {
      std::ofstream ofs(out_file);
      ofs << csv_header << std::endl;
      ofs << csv_output.str();
    }

    APSI_LOG_INFO("Wrote output to " << out_file);
  }
}

void PrintTransmittedData(::apsi::network::Channel &channel) {
  auto nice_byte_count = [](uint64_t bytes) -> std::string {
    std::stringstream ss;
    if (bytes >= 10 * 1024) {
      ss << bytes / 1024 << " KB";
    } else {
      ss << bytes << " B";
    }
    return ss.str();
  };

  SPDLOG_INFO("Communication R->S: {} ", nice_byte_count(channel.bytes_sent()));
  SPDLOG_INFO("Communication S->R: {}",
              nice_byte_count(channel.bytes_received()));
  SPDLOG_INFO("Communication total: {}",
              nice_byte_count(channel.bytes_sent() + channel.bytes_received()));
}

std::string FetchCurveName(CurveType curve_type) {
  std::string curve_name;
  switch (curve_type) {
    case CurveType::CURVE_25519: {
      curve_name = "Curve25519";
      break;
    }
    case CurveType::CURVE_FOURQ: {
      curve_name = "FourQ";
      break;
    }
    case CurveType::CURVE_SM2: {
      curve_name = "sm2";
      break;
    }
    default: {
      YACL_THROW("Invaild curve type");
    }
  }
  return curve_name;
}
}  // namespace psi::dkpir