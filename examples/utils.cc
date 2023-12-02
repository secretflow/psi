// Copyright 2021 Ant Group Co., Ltd.
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

#include "examples/utils.h"

#include "absl/strings/str_split.h"
#include "yacl/link/factory.h"

DEFINE_string(parties, "127.0.0.1:61530,127.0.0.1:61531",
              "server list, format: host1:port1[,host2:port2, ...]");
DEFINE_uint32(rank, 0, "self rank");

std::shared_ptr<yacl::link::Context> MakeLink() {
  yacl::link::ContextDesc lctx_desc;
  std::vector<std::string> hosts = absl::StrSplit(FLAGS_parties, ',');
  for (size_t rank = 0; rank < hosts.size(); rank++) {
    const auto id = fmt::format("party{}", rank);
    lctx_desc.parties.emplace_back(id, hosts[rank]);
  }
  auto lctx = yacl::link::FactoryBrpc().CreateContext(lctx_desc, FLAGS_rank);
  lctx->ConnectToMesh();
  return lctx;
}
