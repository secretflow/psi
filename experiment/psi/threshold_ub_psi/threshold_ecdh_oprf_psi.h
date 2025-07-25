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

#pragma once

#include <vector>

#include "yacl/base/byte_container_view.h"
#include "yacl/link/link.h"

#include "psi/algorithm/ecdh/ub_psi/ecdh_oprf_psi.h"

namespace psi::ecdh {
class ThresholdEcdhOprfPsiServer : public EcdhOprfPsiServer {
 public:
  explicit ThresholdEcdhOprfPsiServer(const EcdhOprfPsiOptions& options)
      : EcdhOprfPsiServer(options), online_link_(options.online_link) {}

  explicit ThresholdEcdhOprfPsiServer(const EcdhOprfPsiOptions& options,
                                      yacl::ByteContainerView private_key)
      : EcdhOprfPsiServer(options, private_key),
        online_link_(options.online_link) {}

  /**
   * @brief recv client's shuffled indexes, use shuffled_seed to restore
   * indexes, then send restored indexes to client
   *
   * @param client_unique_count which is used to generate shuffle index
   */
  void RecvAndRestoreIndexes(uint32_t client_unique_count);

  /**
   * @brief recv the count of unique keys in the actual intersection
   *
   */
  uint32_t RecvIntersectionUniqueKeyCount();

 private:
  std::shared_ptr<yacl::link::Context> online_link_;
};

class ThresholdEcdhOprfPsiClient : public EcdhOprfPsiClient {
 public:
  explicit ThresholdEcdhOprfPsiClient(const EcdhOprfPsiOptions& options)
      : EcdhOprfPsiClient(options), online_link_(options.online_link) {}

  explicit ThresholdEcdhOprfPsiClient(const EcdhOprfPsiOptions& options,
                                      yacl::ByteContainerView private_key)
      : EcdhOprfPsiClient(options, private_key),
        online_link_(options.online_link) {}

  /**
   * @brief send client's shuffled indexes
   *
   */
  void SendShuffledIndexes(const std::vector<uint32_t>& self_indexes);

  /**
   * @brief recv client's restored indexes
   *
   */
  std::vector<uint32_t> RecvRestoredIndexes();

  /**
   * @brief send the count of unique keys in the actual intersection
   *
   */
  void SendIntersectionUniqueKeyCount(
      uint32_t real_intersection_unique_key_count);

 private:
  std::shared_ptr<yacl::link::Context> online_link_;
};

}  // namespace psi::ecdh
