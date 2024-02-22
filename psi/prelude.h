// Copyright 2023 Ant Group Co., Ltd.
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

#include "fmt/ostream.h"

#include "psi/proto/pir.pb.h"
#include "psi/proto/psi.pb.h"
#include "psi/proto/psi_v2.pb.h"

namespace fmt {

template <>
struct formatter<psi::CurveType> : ostream_formatter {};

template <>
struct formatter<psi::PirProtocol> : ostream_formatter {};

template <>
struct formatter<psi::PsiType> : ostream_formatter {};

template <>
struct formatter<psi::v2::Role> : ostream_formatter {};

template <>
struct formatter<psi::v2::Protocol> : ostream_formatter {};

template <>
struct formatter<psi::v2::IoType> : ostream_formatter {};

template <>
struct formatter<psi::v2::PsiConfig::AdvancedJoinType> : ostream_formatter {};

template <>
struct formatter<psi::v2::RecoveryCheckpoint::Stage> : ostream_formatter {};

}  // namespace fmt
