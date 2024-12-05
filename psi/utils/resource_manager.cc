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

#include "psi/utils/resource_manager.h"

#include "spdlog/spdlog.h"
#include "yacl/base/exception.h"

namespace psi {

void DirResource::Acquire() {
  if (!std::filesystem::exists(Name())) {
    SPDLOG_INFO("create path: {}", Name());
    std::filesystem::create_directories(Name());
  }
}

void DirResource::Release() {
  if (std::filesystem::exists(Name())) {
    SPDLOG_INFO("remove path: {}", Name());
    std::filesystem::remove_all(Name());
  }
}

void LinkResource::Acquire() {
  int rank = -1;
  for (size_t i = 0; i < link_desc_.parties.size(); i++) {
    if (link_desc_.parties[i].id == Name()) {
      rank = i;
    }
  }
  YACL_ENFORCE_GE(rank, 0, "Couldn't find rank in YACL Link.");
  link_ = yacl::link::FactoryBrpc().CreateContext(link_desc_, rank);
}

void LinkResource::Release() {
  if (link_) {
    link_->WaitLinkTaskFinish();
    link_.reset();
  }
}

std::shared_ptr<yacl::link::Context> LinkResource::GetLinkContext() const {
  YACL_ENFORCE(link_, "Link is not initialized.");
  return link_;
}

void ResourceManager::AddResource(std::shared_ptr<Resource> resource) {
  resources_.emplace(resource->Name(), resource);
}

void ResourceManager::RemoveResource(const std::string& name) {
  auto iter = resources_.find(name);
  if (iter != resources_.end()) {
    iter->second->Release();
    resources_.erase(iter);
  }
}
void ResourceManager::RemoveAllResource() {
  for (auto& resource : resources_) {
    resource.second->Release();
  }
  resources_.clear();
}

std::shared_ptr<DirResource> ResourceManager::AddDirResouce(
    const std::filesystem::path& path) {
  auto dir_resource = std::make_shared<DirResource>(path);
  dir_resource->Acquire();
  AddResource(dir_resource);
  return dir_resource;
}

std::shared_ptr<LinkResource> ResourceManager::AddLinkResource(
    std::string link_id, yacl::link::ContextDesc link_desc) {
  auto link_resource =
      std::make_shared<LinkResource>(std::move(link_id), std::move(link_desc));
  link_resource->Acquire();
  AddResource(link_resource);
  return link_resource;
}

}  // namespace psi