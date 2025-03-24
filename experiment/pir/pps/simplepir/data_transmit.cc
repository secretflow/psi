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

#include "data_transmit.h"

#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace pir::simple {
std::ostream &operator<<(std::ostream &os, const __uint128_t &value) {
  if (value == 0) {
    return os << "0";
  }

  uint64_t high = value >> 64;
  uint64_t low = static_cast<uint64_t>(value);
  os << "0x" << std::hex << std::setfill('0') << std::setw(16) << high
     << std::setw(16) << low;
  return os;
}

Sender::Sender(const std::string &ip, int port) : ip_(ip), port_(port) {}

void Sender::sendData(const std::vector<__uint128_t> &data) {
  int sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0) {
    std::cerr << "Error: Failed to create socket" << std::endl;
    exit(1);
  }

  struct sockaddr_in serv_addr;
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(port_);
  inet_pton(AF_INET, ip_.c_str(), &serv_addr.sin_addr);

  int res = connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
  if (res < 0) {
    std::cerr << "Error: Failed to connect to server" << std::endl;
    exit(1);
  }

  // send data size
  size_t size = htonl(data.size());
  sendAll(sockfd, &size, sizeof(size));

  // send data
  for (size_t i = 0; i < data.size(); i++) {
    sendAll(sockfd, &data[i], sizeof(data[i]));
  }

  close(sockfd);
}

Receiver::Receiver(int port) : port_(port) {
  sockfd_ = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd_ < 0) {
    std::cerr << "Error: Failed to create socket" << std::endl;
    exit(1);
  }

  struct sockaddr_in serv_addr;
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_addr.s_addr = INADDR_ANY;
  serv_addr.sin_port = htons(port_);

  int res = bind(sockfd_, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
  if (res < 0) {
    std::cerr << "Error: Failed to bind socket" << std::endl;
    exit(1);
  }

  res = listen(sockfd_, 5);
  if (res < 0) {
    std::cerr << "Error: Failed to listen on socket" << std::endl;
    exit(1);
  }
}

Receiver::~Receiver() { close(sockfd_); }

std::vector<__uint128_t> Receiver::receiveData() {
  struct sockaddr_in cli_addr;
  socklen_t clilen = sizeof(cli_addr);
  int newsockfd = accept(sockfd_, (struct sockaddr *)&cli_addr, &clilen);
  if (newsockfd < 0) {
    std::cerr << "Error: Failed to accept connection" << std::endl;
    exit(1);
  }

  // receive data size
  size_t size;
  receiveAll(newsockfd, &size, sizeof(size));
  size = ntohl(size);

  // receive data
  std::vector<__uint128_t> data(size);
  for (size_t i = 0; i < size; i++) {
    receiveAll(newsockfd, &data[i], sizeof(data[i]));
  }

  close(newsockfd);
  return data;
}
}  // namespace pir::simple

