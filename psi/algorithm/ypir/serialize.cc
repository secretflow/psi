#include "psi/algorithm/ypir/serialize.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "yacl/base/exception.h"

namespace psi::ypir {
namespace {

template <typename T>
void AppendPod(std::string& out, const T& value) {
  out.append(reinterpret_cast<const char*>(&value), sizeof(T));
}

template <typename T>
T ReadPod(const char*& ptr, const char* end) {
  YACL_ENFORCE_GE(end - ptr, static_cast<ptrdiff_t>(sizeof(T)));
  T value;
  std::memcpy(&value, ptr, sizeof(T));
  ptr += sizeof(T);
  return value;
}

template <typename T>
void AppendVector(std::string& out, const std::vector<T>& values) {
  AppendPod<uint64_t>(out, values.size());
  if (!values.empty()) {
    out.append(reinterpret_cast<const char*>(values.data()),
               values.size() * sizeof(T));
  }
}

template <typename T>
std::vector<T> ReadVector(const char*& ptr, const char* end) {
  const auto size = ReadPod<uint64_t>(ptr, end);
  YACL_ENFORCE_GE(end - ptr, static_cast<ptrdiff_t>(size * sizeof(T)));
  std::vector<T> out(size);
  if (size > 0) {
    std::memcpy(out.data(), ptr, size * sizeof(T));
    ptr += size * sizeof(T);
  }
  return out;
}

template <typename T>
void AppendVector2D(std::string& out,
                    const std::vector<std::vector<T>>& values) {
  AppendPod<uint64_t>(out, values.size());
  for (const auto& inner : values) {
    AppendVector(out, inner);
  }
}

template <typename T>
std::vector<std::vector<T>> ReadVector2D(const char*& ptr, const char* end) {
  const auto outer = ReadPod<uint64_t>(ptr, end);
  std::vector<std::vector<T>> out;
  out.reserve(outer);
  for (uint64_t i = 0; i < outer; ++i) {
    out.push_back(ReadVector<T>(ptr, end));
  }
  return out;
}

template <typename T>
void AppendVector3D(std::string& out,
                    const std::vector<std::vector<std::vector<T>>>& values) {
  AppendPod<uint64_t>(out, values.size());
  for (const auto& inner : values) {
    AppendVector2D(out, inner);
  }
}

template <typename T>
std::vector<std::vector<std::vector<T>>> ReadVector3D(const char*& ptr,
                                                      const char* end) {
  const auto outer = ReadPod<uint64_t>(ptr, end);
  std::vector<std::vector<std::vector<T>>> out;
  out.reserve(outer);
  for (uint64_t i = 0; i < outer; ++i) {
    out.push_back(ReadVector2D<T>(ptr, end));
  }
  return out;
}

yacl::Buffer ToBuffer(const std::string& bytes) {
  return yacl::Buffer(bytes.data(), bytes.size());
}

}  // namespace

yacl::Buffer SerializeQuery(const YpirQuery& query) {
  std::string bytes;
  AppendPod<uint8_t>(bytes, static_cast<uint8_t>(query.mode));
  AppendVector(bytes, query.packed_query_row);
  AppendVector(bytes, query.qu0);
  AppendVector(bytes, query.qu1);
  AppendVector3D(bytes, query.ksk_b);
  return ToBuffer(bytes);
}

YpirQuery DeserializeQuery(const yacl::ByteContainerView& buffer) {
  const char* ptr = reinterpret_cast<const char*>(buffer.data());
  const char* end = ptr + buffer.size();

  YpirQuery query;
  query.mode = static_cast<YpirMode>(ReadPod<uint8_t>(ptr, end));
  query.packed_query_row = ReadVector<uint64_t>(ptr, end);
  query.qu0 = ReadVector<uint64_t>(ptr, end);
  query.qu1 = ReadVector<uint64_t>(ptr, end);
  query.ksk_b = ReadVector3D<uint64_t>(ptr, end);
  YACL_ENFORCE(ptr == end, "unexpected trailing bytes in YpirQuery");
  return query;
}

yacl::Buffer SerializeResponse(const YpirResponse& response) {
  std::string bytes;
  AppendPod<uint8_t>(bytes, static_cast<uint8_t>(response.mode));
  AppendVector(bytes, response.simplepir_response);
  AppendVector2D(bytes, response.doublepir_response);
  return ToBuffer(bytes);
}

YpirResponse DeserializeResponse(const yacl::ByteContainerView& buffer) {
  const char* ptr = reinterpret_cast<const char*>(buffer.data());
  const char* end = ptr + buffer.size();

  YpirResponse response;
  response.mode = static_cast<YpirMode>(ReadPod<uint8_t>(ptr, end));
  response.simplepir_response = ReadVector<uint64_t>(ptr, end);
  response.doublepir_response = ReadVector2D<uint64_t>(ptr, end);
  YACL_ENFORCE(ptr == end, "unexpected trailing bytes in YpirResponse");
  return response;
}

}  // namespace psi::ypir
