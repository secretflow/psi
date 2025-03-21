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

#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "pir_client.h"
#include "pir_server.h"

int main() {
  size_t n = 1 << 10;
  size_t N = 1ULL << 12;
  size_t q = 1ULL << 32;
  size_t p = 991;
  std::string ip = "127.0.0.1";
  int port = 12345;
  int radius = 4;
  double sigma = 6.8;

  try {
    std::vector<std::vector<__uint128_t>> A;
    A.resize(n);
    size_t row = static_cast<size_t>(sqrt(N));
    for (size_t i = 0; i < n; i++) {
      A[i] = pir::simple::generate_random_vector(row, q);
    }
    std::cout << "A generated" << std::endl;

    pir::simple::PIRServer server(n, q, N, p, ip, port);
    pir::simple::PIRClient client(n, q, N, p, radius, sigma, ip, port);

    server.set_A_(A);
    server.generate_database();
    client.matrix_transpose_128(A);
    auto server_thread = std::thread([&server]() { server.server_setup(); });
    auto client_thread = std::thread([&client]() { client.client_setup(); });
    server_thread.join();
    client_thread.join();

    size_t idx = 10;

    server_thread = std::thread([&server]() { server.server_query(); });
    client_thread = std::thread([&client, idx]() { client.client_query(idx); });

    server_thread.join();
    client_thread.join();

    server_thread = std::thread([&server]() { server.server_answer(); });
    client_thread = std::thread([&client]() { client.client_answer(); });

    server_thread.join();
    client_thread.join();

    client.client_recover();
    server.get_value(idx);
  } catch (const std::exception &e) {
    std::cerr << e.what() << std::endl;
  }
  return 0;
}
