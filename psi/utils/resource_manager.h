// Copyright 2024 Ant Group Co., Ltd.
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

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

#include "yacl/link/link.h"

namespace psi {

class Resource {
 public:
  explicit Resource(std::string name) : name_(std::move(name)) {}
  virtual ~Resource() = default;
  virtual void Acquire() = 0;
  virtual void Release() = 0;
  std::string Name() const { return name_; }

 private:
  std::string name_;
};

class DirResource : public Resource {
 public:
  explicit DirResource(const std::filesystem::path& path)
      : Resource(path.string()), path_(path) {}

  void Acquire() override;
  void Release() override;

  std::filesystem::path Path() const { return path_; }

 private:
  std::filesystem::path path_;
};

class LinkResource : public Resource {
 public:
  explicit LinkResource(std::string link_id, yacl::link::ContextDesc link_desc)
      : Resource(std::move(link_id)), link_desc_(std::move(link_desc)) {}

  void Acquire() override;
  void Release() override;
  std::shared_ptr<yacl::link::Context> GetLinkContext() const;

 private:
  yacl::link::ContextDesc link_desc_;
  std::shared_ptr<yacl::link::Context> link_;
};

class ResourceManager {
 public:
  ~ResourceManager() { RemoveAllResource(); }

  static ResourceManager& GetInstance() {
    static ResourceManager instance;
    return instance;
  }
  std::shared_ptr<DirResource> AddDirResouce(const std::filesystem::path& path);

  std::shared_ptr<LinkResource> AddLinkResource(
      std::string link_id, yacl::link::ContextDesc link_desc);

  void AddResource(std::shared_ptr<Resource> resource);
  void RemoveResource(const std::string& name);
  void RemoveAllResource();

 private:
  ResourceManager() = default;
  std::unordered_map<std::string, std::shared_ptr<Resource>> resources_;
};

}  // namespace psi
