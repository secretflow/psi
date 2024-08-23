// Copyright 2024 Ant Group Co., Ltd.
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

// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

#include "psi/apsi_wrapper/utils/sender_db.h"

#include <fstream>
#include <iostream>
#include <string_view>
#include <utility>

#include "psi/apsi_wrapper/utils/common.h"

#if defined(__GNUC__) && (__GNUC__ < 8) && !defined(__clang__)
#include <experimental/filesystem>
#else
#include <filesystem>
#endif

using namespace std;
#if defined(__GNUC__) && (__GNUC__ < 8) && !defined(__clang__)
namespace fs = std::experimental::filesystem;
#else
namespace fs = std::filesystem;
#endif

namespace psi::apsi_wrapper {

shared_ptr<::apsi::sender::SenderDB> TryLoadSenderDB(
    std::istream &in_stream, ::apsi::oprf::OPRFKey &oprf_key) {
  shared_ptr<::apsi::sender::SenderDB> result = nullptr;

  in_stream.exceptions(ios_base::badbit | ios_base::failbit);
  try {
    auto [data, size] = ::apsi::sender::SenderDB::Load(in_stream);
    APSI_LOG_INFO("Loaded SenderDB (" << size << " bytes) ");

    result = make_shared<::apsi::sender::SenderDB>(std::move(data));

    // Load also the OPRF key
    oprf_key.load(in_stream);
    APSI_LOG_INFO("Loaded OPRF key (" << ::apsi::oprf::oprf_key_size
                                      << " bytes)");
  } catch (const exception &e) {
    // Failed to load SenderDB
    APSI_LOG_DEBUG("Failed to load SenderDB: " << e.what());
  }

  return result;
}

bool TrySaveSenderDB(std::ostream &os,
                     shared_ptr<::apsi::sender::SenderDB> sender_db,
                     const ::apsi::oprf::OPRFKey &oprf_key) {
  if (!sender_db) {
    return false;
  }

  os.exceptions(ios_base::badbit | ios_base::failbit);
  try {
    size_t size = sender_db->save(os);
    APSI_LOG_INFO("Saved SenderDB (" << size << " bytes)");

    // Save also the OPRF key (fixed size: oprf_key_size bytes)
    oprf_key.save(os);
    APSI_LOG_INFO("Saved OPRF key (" << ::apsi::oprf::oprf_key_size
                                     << " bytes)");

  } catch (const exception &e) {
    APSI_LOG_WARNING("Failed to save SenderDB: " << e.what());
    return false;
  }

  return true;
}

shared_ptr<::apsi::sender::SenderDB> TryLoadSenderDB(
    const std::string &db_file, const std::string &params_file,
    ::apsi::oprf::OPRFKey &oprf_key) {
  shared_ptr<::apsi::sender::SenderDB> result = nullptr;

  ifstream fs(db_file, ios::binary);
  fs.exceptions(ios_base::badbit | ios_base::failbit);
  try {
    auto [data, size] = ::apsi::sender::SenderDB::Load(fs);
    APSI_LOG_INFO("Loaded SenderDB (" << size << " bytes) from " << db_file);
    if (!params_file.empty()) {
      APSI_LOG_WARNING(
          "PSI parameters were loaded with the SenderDB; "
          "ignoring given PSI parameters");
    }
    result = make_shared<::apsi::sender::SenderDB>(std::move(data));

    // Load also the OPRF key
    oprf_key.load(fs);
    APSI_LOG_INFO("Loaded OPRF key (" << ::apsi::oprf::oprf_key_size
                                      << " bytes) from " << db_file);
  } catch (const exception &e) {
    // Failed to load SenderDB
    APSI_LOG_DEBUG("Failed to load SenderDB: " << e.what());
  }

  return result;
}

shared_ptr<::apsi::sender::SenderDB> GenerateSenderDB(
    const std::string &db_file, const std::string &params_file,
    size_t nonce_byte_count, bool compress, ::apsi::oprf::OPRFKey &oprf_key,
    const std::vector<std::string> &keys,
    const std::vector<std::string> &labels) {
  unique_ptr<::apsi::PSIParams> params = BuildPsiParams(params_file);
  if (!params) {
    // We must have valid parameters given
    APSI_LOG_ERROR("Failed to set PSI parameters");
    return nullptr;
  }

  unique_ptr<DBData> db_data;
  if (db_file.empty() || !(db_data = load_db(db_file, keys, labels))) {
    // Failed to read db file
    APSI_LOG_DEBUG("Failed to load data from a CSV file");
    return nullptr;
  }

  return create_sender_db(*db_data, std::move(params), oprf_key,
                          nonce_byte_count, compress);
}

bool TrySaveSenderDB(const std::string &sdb_out_file,
                     shared_ptr<::apsi::sender::SenderDB> sender_db,
                     const ::apsi::oprf::OPRFKey &oprf_key) {
  if (!sender_db) {
    return false;
  }

  ofstream fs(sdb_out_file, ios::binary);
  fs.exceptions(ios_base::badbit | ios_base::failbit);
  try {
    size_t size = sender_db->save(fs);
    APSI_LOG_INFO("Saved SenderDB (" << size << " bytes) to " << sdb_out_file);

    // Save also the OPRF key (fixed size: oprf_key_size bytes)
    oprf_key.save(fs);
    APSI_LOG_INFO("Saved OPRF key (" << ::apsi::oprf::oprf_key_size
                                     << " bytes) to " << sdb_out_file);

  } catch (const exception &e) {
    APSI_LOG_WARNING("Failed to save SenderDB: " << e.what());
    return false;
  }

  return true;
}

unique_ptr<psi::apsi_wrapper::DBData> load_db(
    const string &db_file, const std::vector<std::string> &keys,
    const std::vector<std::string> &labels) {
  psi::apsi_wrapper::DBData db_data;
  try {
    if (keys.empty() && labels.empty()) {
      psi::apsi_wrapper::ApsiCsvReader reader(db_file);
      tie(db_data, ignore) = reader.read();
    }
  } catch (const exception &ex) {
    APSI_LOG_WARNING("Could not open or read file `" << db_file
                                                     << "`: " << ex.what());
    return nullptr;
  }

  return make_unique<psi::apsi_wrapper::DBData>(std::move(db_data));
}

pair<unique_ptr<psi::apsi_wrapper::DBData>, vector<std::string>>
load_db_with_orig_items(const std::string &db_file) {
  psi::apsi_wrapper::DBData db_data;
  std::vector<std::string> orig_items;
  try {
    psi::apsi_wrapper::ApsiCsvReader reader(db_file);
    tie(db_data, orig_items) = reader.read();
  } catch (const exception &ex) {
    YACL_THROW("load file: {} failed: {}", db_file, ex.what());
  }

  return {make_unique<psi::apsi_wrapper::DBData>(std::move(db_data)),
          std::move(orig_items)};
}

shared_ptr<::apsi::sender::SenderDB> create_sender_db(
    const psi::apsi_wrapper::DBData &db_data,
    unique_ptr<::apsi::PSIParams> psi_params, ::apsi::oprf::OPRFKey &oprf_key,
    size_t nonce_byte_count, bool compress) {
  if (!psi_params) {
    APSI_LOG_ERROR("No PSI parameters were given");
    return nullptr;
  }

  shared_ptr<::apsi::sender::SenderDB> sender_db;
  if (holds_alternative<psi::apsi_wrapper::UnlabeledData>(db_data)) {
    try {
      sender_db =
          make_shared<::apsi::sender::SenderDB>(*psi_params, 0, 0, compress);
      sender_db->set_data(get<psi::apsi_wrapper::UnlabeledData>(db_data));

      APSI_LOG_INFO("Created unlabeled SenderDB with "
                    << sender_db->get_item_count() << " items");
    } catch (const exception &ex) {
      APSI_LOG_ERROR("Failed to create SenderDB: " << ex.what());
      return nullptr;
    }
  } else if (holds_alternative<psi::apsi_wrapper::LabeledData>(db_data)) {
    try {
      auto &labeled_db_data = get<psi::apsi_wrapper::LabeledData>(db_data);

      // Find the longest label and use that as label size
      size_t label_byte_count =
          max_element(labeled_db_data.begin(), labeled_db_data.end(),
                      [](auto &a, auto &b) {
                        return a.second.size() < b.second.size();
                      })
              ->second.size();

      sender_db = make_shared<::apsi::sender::SenderDB>(
          *psi_params, label_byte_count, nonce_byte_count, compress);
      sender_db->set_data(labeled_db_data);
      APSI_LOG_INFO("Created labeled SenderDB with "
                    << sender_db->get_item_count() << " items and "
                    << label_byte_count << "-byte labels (" << nonce_byte_count
                    << "-byte nonces)");
    } catch (const exception &ex) {
      APSI_LOG_ERROR("Failed to create SenderDB: " << ex.what());
      return nullptr;
    }
  } else {
    // Should never reach this point
    APSI_LOG_ERROR("Loaded database is in an invalid state");
    return nullptr;
  }

  if (compress) {
    APSI_LOG_INFO("Using in-memory compression to reduce memory footprint");
  }

  // Read the OPRFKey and strip the SenderDB to
  // reduce memory use
  oprf_key = sender_db->strip();

  APSI_LOG_INFO("SenderDB packing rate: " << sender_db->get_packing_rate());

  return sender_db;
}

}  // namespace psi::apsi_wrapper
