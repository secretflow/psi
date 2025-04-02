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

#include <string>
#include <vector>

namespace pir::simple {
Sender::Sender(const std::string &ip, int port) : ip_(ip), port_(port) {}

void Sender::sendData(const std::vector<uint64_t> &data) {
  // Create TCP socket
  int sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0) {
    SPDLOG_ERROR("Error: Failed to create socket.");
  }

  // Configure server address structure
  struct sockaddr_in serv_addr;
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(port_);  // Convert to network byte order
  inet_pton(AF_INET, ip_.c_str(), &serv_addr.sin_addr);

  // Establish connection with server
  int res = connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
  if (res < 0) {
    SPDLOG_ERROR("Error: Failed to connect socket.");
  }

  // Send data size header (network byte order)
  size_t size = htonl(data.size());
  sendAll(sockfd, &size, sizeof(size));

  // Send payload data elements
  for (size_t i = 0; i < data.size(); i++) {
    sendAll(sockfd, &data[i], sizeof(data[i]));
  }

  // Cleanup socket resources
  close(sockfd);
}

Receiver::Receiver(int port) : port_(port) {
  // Create listening socket
  sockfd_ = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd_ < 0) {
    SPDLOG_ERROR("Error: Failed to create socket.");
  }

  // Configure server address
  struct sockaddr_in serv_addr;
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_addr.s_addr = INADDR_ANY;  // Bind to all interfaces
  serv_addr.sin_port = htons(port_);

  // Bind and listen
  int res = bind(sockfd_, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
  if (res < 0) {
    SPDLOG_ERROR("Error: Failed to bind socket.");
  }

  res = listen(sockfd_, 5);  // Backlog of 5 connections
  if (res < 0) {
    SPDLOG_ERROR("Error: Failed to listen socket.");
  }
}

Receiver::~Receiver() { close(sockfd_); }

std::vector<uint64_t> Receiver::receiveData() {
  // Accept incoming connection
  struct sockaddr_in cli_addr;
  socklen_t clilen = sizeof(cli_addr);
  int newsockfd = accept(sockfd_, (struct sockaddr *)&cli_addr, &clilen);
  if (newsockfd < 0) {
    SPDLOG_ERROR("Error: Failed to accept socket.");
  }

  // Receive data size header
  size_t size;
  receiveAll(newsockfd, &size, sizeof(size));
  size = ntohl(size);  // Convert to host byte order

  // Receive payload data
  std::vector<uint64_t> data(size);
  for (size_t i = 0; i < size; i++) {
    receiveAll(newsockfd, &data[i], sizeof(data[i]));
  }

  close(newsockfd);
  return data;
}
}  // namespace pir::simple
