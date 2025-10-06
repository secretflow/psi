#include <iostream>
#include <vector>
#include <unordered_map>
#include <chrono>

void fun1(int n) {
  auto start = std::chrono::high_resolution_clock::now();
  std::vector<uint32_t> test_vec;

  for (int i = 0; i < n; i++) {
    test_vec.push_back(i);
  }

  auto end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double, std::milli> elapsed = end - start;
  std::cout << "fun1耗时: " << elapsed.count() << " ms" << std::endl;
}

void fun2(int n) {
  auto start = std::chrono::high_resolution_clock::now();
  
  std::vector<uint32_t> test_vec;
  test_vec.resize(n);

  for (int i = 0; i < n; i++) {
    test_vec[i] = i;
  }

  auto end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double, std::milli> elapsed = end - start;
  std::cout << "fun2耗时: " << elapsed.count() << " ms" << std::endl;
}

void fun3(int n) {
  auto start = std::chrono::high_resolution_clock::now();
  std::vector<uint32_t> test_vec;

  for (int i = 0; i < n; i++) {
    test_vec.emplace_back(i);
  }

  auto end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double, std::milli> elapsed = end - start;
  std::cout << "fun3耗时: " << elapsed.count() << " ms" << std::endl;
}

void fun4(int n) {
  auto start = std::chrono::high_resolution_clock::now();
  std::unordered_map<uint32_t, uint32_t> test_map;

  test_map[1] = 1;
  test_map[2] = 2;
  test_map[3] = 3;
  uint32_t key = 0;

  for (int i = 0; i < n; i++) {
    key = test_map[i];
  }

  std::cout << key << std::endl;

  auto end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double, std::milli> elapsed = end - start;
  std::cout << "fun4耗时: " << elapsed.count() << " ms" << std::endl;
}

void fun5(int n) {
  auto start = std::chrono::high_resolution_clock::now();
  std::vector<uint32_t> test_vec(n);

  std::unordered_map<uint32_t, uint32_t> test_map;

  test_map[1] = 1;
  test_map[2] = 2;
  test_map[3] = 3;

  for (auto &[k,v] : test_map) {
    test_vec[k] = v;
  }

  uint32_t key = 0;

  for (int i = 0; i < n; i++) {
    key = test_vec[i];
  }

  std::cout << key << std::endl;

  auto end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double, std::milli> elapsed = end - start;
  std::cout << "fun5耗时: " << elapsed.count() << " ms" << std::endl;

}

struct ShuffledBatch {
  std::vector<std::string> batch_items;
  std::vector<size_t> batch_indices;
  std::vector<size_t> shuffled_indices;
  std::vector<uint32_t> dup_cnts;
};
int main() {
  ShuffledBatch batch;
  batch.batch_indices.emplace_back(1);
  batch.shuffled_indices.emplace_back(1);
  batch.dup_cnts.emplace_back(1);

  batch.batch_indices.emplace_back(2);
  batch.shuffled_indices.emplace_back(2);
  batch.dup_cnts.emplace_back(2);

  batch.batch_indices.emplace_back(3);
  batch.shuffled_indices.emplace_back(3);
  batch.dup_cnts.emplace_back(3);

  std::cout << batch.batch_items.size() << std::endl;
  std::cout << batch.batch_indices.size() << std::endl;
  std::cout << batch.shuffled_indices.size() << std::endl;
  std::cout << batch.dup_cnts.size() << std::endl;

  // fun1(100000);
  // fun2(100000);
  // fun3(100000);
  fun4(1000000);
  fun5(1000000);
 
  
}