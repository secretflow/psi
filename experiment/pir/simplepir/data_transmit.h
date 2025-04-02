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
#include <spdlog/spdlog.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace pir::simple {
/**
 * TCP-based network sender for PIR protocol communications
 * Implements reliable data transmission with error handling
 */
class Sender {
 public:
  /**
   * Constructs sender targeting specific network endpoint
   * @param ip Destination IP address (default: localhost)
   * @param port Destination port number (default: 12345)
   */
  Sender(const std::string &ip, int port);

  /**
   * Transmits unsigned integer vector over established connection
   * @param data Vector to serialize and send
   */
  void sendData(const std::vector<uint64_t> &data);

 private:
  std::string ip_ = "127.0.0.1";
  int port_ = 12345;

  /**
   * Guaranteed delivery send implementation
   * @param sockfd Connected socket descriptor
   * @param data Raw byte buffer to transmit
   * @param size Buffer size in bytes
   *
   * Features:
   * - Partial send retry handling
   * - Error logging via SPDLOG
   * - Non-blocking socket not supported
   */
  void sendAll(int sockfd, const void *data, size_t size) {
    size_t sent = 0;
    while (sent < size) {  // Persist until full payload delivered
      ssize_t res =
          send(sockfd, static_cast<const char *>(data) + sent, size - sent, 0);
      if (res < 0) {
        SPDLOG_ERROR("Send error.");
      }
      sent += res;  // Update progress counter
    }
  }
};

/**
 * TCP network receiver for PIR protocol communications
 * Implements blocking receive with full payload collection
 */
class Receiver {
 public:
  /**
   * Constructs receiver bound to specific port
   * @param port Listening port number (default: 12345)
   */
  explicit Receiver(int port);

  /**
   * Destructor ensures socket resource cleanup
   */
  ~Receiver();

  /**
   * Receives and deserializes unsigned integer vector
   * @return Received data vector
   */
  std::vector<uint64_t> receiveData();

 private:
  int port_ = 12345;
  int sockfd_ = 0;

  /**
   * Full payload reception implementation
   * @param sockfd Connected socket descriptor
   * @param data Buffer for received bytes
   * @param size Expected payload size
   *
   * Features:
   * - Blocks until full payload received
   * - Handles partial receives
   * - Logs errors without throwing
   */
  void receiveAll(int sockfd, void *data, size_t size) {
    size_t received = 0;
    while (received < size) {
      ssize_t res = recv(sockfd, static_cast<char *>(data) + received,
                         size - received, 0);
      if (res < 0) {
        SPDLOG_ERROR("Receive error.");
      }
      received += res;
    }
  }
};
}  // namespace pir::simple
