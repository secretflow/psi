// Copyright 2025 The secretflow authors.
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

#include <arpa/inet.h>
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace pir::simple {
// print __uint128_t
std::ostream &operator<<(std::ostream &os, const __uint128_t &value);

class Sender {
 public:
  Sender(const std::string &ip, int port);

  void sendData(const std::vector<__uint128_t> &data);

 private:
  std::string ip_;
  int port_;

  void sendAll(int sockfd, const void *data, size_t size) {
    size_t sent = 0;
    while (sent < size) {
      ssize_t res =
          send(sockfd, static_cast<const char *>(data) + sent, size - sent, 0);
      if (res < 0) {
        perror("send");
        exit(1);
      }
      sent += res;
    }
  }
};

class Receiver {
 public:
  explicit Receiver(int port);

  ~Receiver();

  std::vector<__uint128_t> receiveData();

 private:
  int port_;
  int sockfd_;

  void receiveAll(int sockfd, void *data, size_t size) {
    size_t received = 0;
    while (received < size) {
      ssize_t res = recv(sockfd, static_cast<char *>(data) + received,
                         size - received, 0);
      if (res < 0) {
        perror("recv");
        exit(1);
      }
      received += res;
    }
  }
};
}  // namespace pir::simple
