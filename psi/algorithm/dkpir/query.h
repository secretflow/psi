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
#include <memory>

#include "apsi/query.h"

namespace psi::dkpir {
using QueryRequest = std::unique_ptr<::apsi::network::SenderOperationQuery>;

// A query request can be used to generate a DkPirQuery, which can be used to
// query both data and row count
class DkPirQuery : public ::apsi::sender::Query {
 public:
  using ::apsi::sender::Query::Query;
  DkPirQuery(QueryRequest query_request,
             std::shared_ptr<::apsi::sender::SenderDB> sender_db,
             std::shared_ptr<::apsi::sender::SenderDB> sender_cnt_db)
      : ::apsi::sender::Query(std::move(query_request), sender_db),
        sender_cnt_db_(std::move(sender_cnt_db)){};

  std::shared_ptr<::apsi::sender::SenderDB> sender_cnt_db() const noexcept {
    return sender_cnt_db_;
  }

 private:
  std::shared_ptr<::apsi::sender::SenderDB> sender_cnt_db_;
};
}  // namespace psi::dkpir