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

#include "pybind11/functional.h"
#include "pybind11/pybind11.h"
#include "pybind11/stl.h"
#include "yacl/base/exception.h"
#include "yacl/link/algorithm/allgather.h"
#include "yacl/link/algorithm/barrier.h"
#include "yacl/link/algorithm/broadcast.h"
#include "yacl/link/algorithm/gather.h"
#include "yacl/link/algorithm/scatter.h"
#include "yacl/link/context.h"
#include "yacl/link/factory.h"

#include "psi/launch.h"
#include "psi/legacy/memory_psi.h"
#include "psi/utils/progress.h"
#include "psi/version.h"

#include "psi/proto/pir.pb.h"
#include "psi/proto/psi.pb.h"
#include "psi/proto/psi_v2.pb.h"

namespace py = pybind11;

namespace brpc {

DECLARE_uint64(max_body_size);
DECLARE_int64(socket_max_unwritten_bytes);

}  // namespace brpc

#define NO_GIL py::call_guard<py::gil_scoped_release>()

namespace psi {

void BindLink(py::module& m) {
  using yacl::link::CertInfo;
  using yacl::link::Context;
  using yacl::link::ContextDesc;
  using yacl::link::RetryOptions;
  using yacl::link::SSLOptions;
  using yacl::link::VerifyOptions;

  // TODO(jint) expose this tag to python?
  constexpr char PY_CALL_TAG[] = "PY_CALL";

  m.doc() = R"pbdoc(
              SPU Link Library
                  )pbdoc";

  py::class_<CertInfo>(m, "CertInfo", "The config info used for certificate")
      .def_readwrite("certificate_path", &CertInfo::certificate_path,
                     "certificate file path")
      .def_readwrite("private_key_path", &CertInfo::private_key_path,
                     "private key file path");

  py::class_<VerifyOptions>(m, "VerifyOptions",
                            "The options used for verify certificate")
      .def_readwrite("verify_depth", &VerifyOptions::verify_depth,
                     "maximum depth of the certificate chain for verification")
      .def_readwrite("ca_file_path", &VerifyOptions::ca_file_path,
                     "the trusted CA file path");

  py::class_<RetryOptions>(m, "RetryOptions",
                           "The options used for channel retry")
      .def_readwrite("max_retry", &RetryOptions::max_retry, "max retry count")
      .def_readwrite("retry_interval_ms", &RetryOptions::retry_interval_ms,
                     "first retry interval")
      .def_readwrite("retry_interval_incr_ms",
                     &RetryOptions::retry_interval_incr_ms,
                     "the amount of time to increase between retries")
      .def_readwrite("max_retry_interval_ms",
                     &RetryOptions::max_retry_interval_ms,
                     "the max interval between retries")
      .def_readwrite("error_codes", &RetryOptions::error_codes,
                     "retry on these error codes, if empty, retry on all codes")
      .def_readwrite(
          "http_codes", &RetryOptions::http_codes,
          "retry on these http codes, if empty, retry on all http codes")
      .def_readwrite("aggressive_retry", &RetryOptions::aggressive_retry,
                     "do aggressive retry");

  py::class_<SSLOptions>(m, "SSLOptions", "The options used for ssl")
      .def_readwrite("cert", &SSLOptions::cert,
                     "certificate used for authentication")
      .def_readwrite("verify", &SSLOptions::verify,
                     "options used to verify the peer's certificate");

  py::class_<ContextDesc::Party>(
      m, "Party", "The party that participate the secure computation")
      .def_readonly("id", &ContextDesc::Party::id, "the id, unique per link")
      .def_readonly("host", &ContextDesc::Party::host, "host address")
      .def("__repr__", [](const ContextDesc::Party& self) {
        return fmt::format("Party(id={}, host={})", self.id, self.host);
      });

  py::class_<ContextDesc>(
      m, "Desc", "Link description, describes parties which joins the link")
      .def(py::init<>())
      .def_readwrite("id", &ContextDesc::id, "the uuid")
      .def_readonly("parties", &ContextDesc::parties,
                    "the parties that joins the computation")
      .def_readwrite("connect_retry_times", &ContextDesc::connect_retry_times,
                     "connect to mesh retry time")
      .def_readwrite("connect_retry_interval_ms",
                     &ContextDesc::connect_retry_interval_ms)
      .def_readwrite("recv_timeout_ms", &ContextDesc::recv_timeout_ms)
      .def_readwrite("http_max_payload_size",
                     &ContextDesc::http_max_payload_size)
      .def_readwrite("http_timeout_ms", &ContextDesc::http_timeout_ms)
      .def_readwrite("brpc_channel_protocol",
                     &ContextDesc::brpc_channel_protocol)
      .def_readwrite("brpc_channel_connection_type",
                     &ContextDesc::brpc_channel_connection_type)
      .def_readwrite("throttle_window_size", &ContextDesc::throttle_window_size)
      .def_readwrite("enable_ssl", &ContextDesc::enable_ssl)
      .def_readwrite("client_ssl_opts", &ContextDesc::client_ssl_opts)
      .def_readwrite("server_ssl_opts", &ContextDesc::server_ssl_opts)
      .def_readwrite("link_type", &ContextDesc::link_type)
      .def_readwrite("retry_opts", &ContextDesc::retry_opts)
      .def_readwrite("disable_msg_seq_id", &ContextDesc::disable_msg_seq_id)
      .def(
          "add_party",
          [](ContextDesc& desc, std::string id, std::string host) {
            desc.parties.push_back({std::move(id), std::move(host)});
          },
          "add a party to the link");

  // expose shared_ptr<Context> to py
  py::class_<Context, std::shared_ptr<Context>>(m, "Context", "the link handle")
      .def("__repr__",
           [](const Context* self) {
             return fmt::format("Link(id={}, rank={}/{})", self->Id(),
                                self->Rank(), self->WorldSize());
           })
      .def(
          "id", [](const Context* self) { return self->Id(); },
          "the unique link id")
      .def_property_readonly(
          "rank", [](const Context* self) { return self->Rank(); },
          py::return_value_policy::copy, "my rank of the link")
      .def_property_readonly(
          "world_size", [](const Context* self) { return self->WorldSize(); },
          py::return_value_policy::copy, "the number of parties")
      .def(
          "spawn",
          [](const std::shared_ptr<Context>& self) {
            return std::shared_ptr<Context>(self->Spawn());
          },
          NO_GIL, "spawn a sub-link, advanced skill")
      .def(
          "barrier",
          [&PY_CALL_TAG](const std::shared_ptr<Context>& self) -> void {
            return yacl::link::Barrier(self, PY_CALL_TAG);
          },
          NO_GIL,
          "Blocks until all parties have reached this routine, aka MPI_Barrier")
      .def(
          "all_gather",
          [&PY_CALL_TAG](const std::shared_ptr<Context>& self,
                         const std::string& in) -> std::vector<std::string> {
            auto bufs = yacl::link::AllGather(self, in, PY_CALL_TAG);
            std::vector<std::string> ret(bufs.size());
            for (size_t idx = 0; idx < bufs.size(); ++idx) {
              ret[idx] = std::string(bufs[idx].data<char>(), bufs[idx].size());
            }
            return ret;
          },
          NO_GIL,
          "Gathers data from all parties and distribute the combined data to "
          "all parties, aka MPI_AllGather")
      .def(
          "gather",
          [&PY_CALL_TAG](const std::shared_ptr<Context>& self,
                         const std::string& in,
                         size_t root) -> std::vector<std::string> {
            auto bufs = yacl::link::Gather(self, in, root, PY_CALL_TAG);
            std::vector<std::string> ret(bufs.size());
            for (size_t idx = 0; idx < bufs.size(); ++idx) {
              ret[idx] = std::string(bufs[idx].data<char>(), bufs[idx].size());
            }
            return ret;
          },
          NO_GIL, "Gathers values from other parties, aka MPI_Gather")
      .def(
          "broadcast",
          [&PY_CALL_TAG](const std::shared_ptr<Context>& self,
                         const std::string& in, size_t root) -> std::string {
            auto buf = yacl::link::Broadcast(self, in, root, PY_CALL_TAG);
            return {buf.data<char>(), static_cast<size_t>(buf.size())};
          },
          NO_GIL,
          "Broadcasts a message from the party with rank 'root' to all other "
          "parties, aka MPI_Bcast")
      .def(
          "stop_link",
          [](const std::shared_ptr<Context>& self) -> void {
            return self->WaitLinkTaskFinish();
          },
          NO_GIL, "Blocks until all link is safely stoped")
      .def(
          "scatter",
          [&PY_CALL_TAG](const std::shared_ptr<Context>& self,
                         const std::vector<std::string>& in,
                         size_t root) -> std::string {
            auto buf = yacl::link::Scatter(self, {in.begin(), in.end()}, root,
                                           PY_CALL_TAG);
            return {buf.data<char>(), static_cast<size_t>(buf.size())};
          },
          NO_GIL,
          "Sends data from one party to all other parties, aka MPI_Scatter")
      .def(
          "send",
          [&PY_CALL_TAG](const std::shared_ptr<Context>& self, size_t dst_rank,
                         const std::string& in) {
            self->Send(dst_rank, in, PY_CALL_TAG);
          },
          NO_GIL, "Sends data to dst_rank")
      .def(
          "send_async",
          [&PY_CALL_TAG](const std::shared_ptr<Context>& self, size_t dst_rank,
                         const std::string& in) {
            self->SendAsync(dst_rank, yacl::Buffer(in), PY_CALL_TAG);
          },
          NO_GIL, "Sends data to dst_rank asynchronously")
      .def(
          "recv",
          [&PY_CALL_TAG](const std::shared_ptr<Context>& self,
                         size_t src_rank) -> py::bytes {
            py::gil_scoped_release release;
            yacl::Buffer buf = self->Recv(src_rank, PY_CALL_TAG);
            py::gil_scoped_acquire acquire;
            return py::bytes{buf.data<char>(), static_cast<size_t>(buf.size())};
          },  // Since it uses py bytes, we cannot release GIL here
          "Receives data from src_rank")
      .def(
          "next_rank",
          [](const std::shared_ptr<Context>& self, size_t strides = 1) {
            return self->NextRank(strides);
          },
          NO_GIL, "Gets next party rank", py::arg("strides") = 1);

  m.def(
      "create_brpc",
      [](const ContextDesc& desc, size_t self_rank,
         bool log_details) -> std::shared_ptr<Context> {
        py::gil_scoped_release release;
        brpc::FLAGS_max_body_size = std::numeric_limits<uint64_t>::max();
        brpc::FLAGS_socket_max_unwritten_bytes =
            std::numeric_limits<int64_t>::max() / 2;

        auto ctx = yacl::link::FactoryBrpc().CreateContext(desc, self_rank);
        ctx->ConnectToMesh(log_details ? spdlog::level::info
                                       : spdlog::level::debug);
        return ctx;
      },
      py::arg("desc"), py::arg("self_rank"), py::kw_only(),
      py::arg("log_details") = false);

  m.def("create_mem",
        [](const ContextDesc& desc,
           size_t self_rank) -> std::shared_ptr<Context> {
          py::gil_scoped_release release;

          auto ctx = yacl::link::FactoryMem().CreateContext(desc, self_rank);
          ctx->ConnectToMesh();
          return ctx;
        });
}

void BindLibs(py::module& m) {
  py::class_<psi::Progress::Data>(m, "ProgressData", "The progress data")
      .def(py::init<>())
      .def_readonly("total", &psi::Progress::Data::total,
                    "the number of all subjobs")
      .def_readonly("finished", &psi::Progress::Data::finished,
                    "the number of finished subjobs")
      .def_readonly("running", &psi::Progress::Data::running,
                    "the number of running subjobs")
      .def_readonly("percentage", &psi::Progress::Data::percentage,
                    "the percentage of the task progress")
      .def_readonly("description", &psi::Progress::Data::description,
                    "description of the current running subjob");

  m.def(
      "mem_psi",
      [](const std::shared_ptr<yacl::link::Context>& lctx,
         const std::string& config_pb,
         const std::vector<std::string>& items) -> std::vector<std::string> {
        psi::MemoryPsiConfig config;
        YACL_ENFORCE(config.ParseFromString(config_pb));

        psi::MemoryPsi psi(config, lctx);
        return psi.Run(items);
      },
      NO_GIL);

  m.def(
      "bucket_psi",
      [](const std::shared_ptr<yacl::link::Context>& lctx,
         const std::string& config_pb,
         psi::ProgressCallbacks progress_callbacks,
         int64_t callbacks_interval_ms, bool ic_mode) -> py::bytes {
        psi::BucketPsiConfig config;
        YACL_ENFORCE(config.ParseFromString(config_pb));

        auto r = psi::RunLegacyPsi(config, lctx, std::move(progress_callbacks),
                                   callbacks_interval_ms, ic_mode);
        return r.SerializeAsString();
      },
      py::arg("link_context"), py::arg("psi_config"),
      py::arg("progress_callbacks") = nullptr,
      py::arg("callbacks_interval_ms") = 5 * 1000, py::arg("ic_mode") = false,
      "Run bucket psi. ic_mode means run in interconnection mode", NO_GIL);

  m.def(
      "psi",
      [](const std::string& config_pb,
         const std::shared_ptr<yacl::link::Context>& lctx) -> py::bytes {
        psi::v2::PsiConfig psi_config;
        YACL_ENFORCE(psi_config.ParseFromString(config_pb));

        auto report = psi::RunPsi(psi_config, lctx);
        return report.SerializeAsString();
      },
      py::arg("psi_config"), py::arg("link_context"), "Run PSI with v2 API.",
      NO_GIL);

  m.def(
      "ub_psi",
      [](const std::string& config_pb,
         const std::shared_ptr<yacl::link::Context>& lctx) -> py::bytes {
        psi::v2::UbPsiConfig ub_psi_config;
        YACL_ENFORCE(ub_psi_config.ParseFromString(config_pb));

        auto report = psi::RunUbPsi(ub_psi_config, lctx);
        return report.SerializeAsString();
      },
      py::arg("ub_psi_config"), py::arg("link_context") = nullptr,
      "Run UB PSI with v2 API.", NO_GIL);

  m.def(
      "apsi_send",
      [](const std::string& config_pb,
         const std::shared_ptr<yacl::link::Context>& lctx) -> py::bytes {
        psi::ApsiSenderConfig config;
        YACL_ENFORCE(config.ParseFromString(config_pb));

        auto r = psi::RunPir(config, lctx);
        return r.SerializeAsString();
      },
      py::arg("pir_config"), py::arg("link_context") = nullptr,
      "Run APSI sender operations.");

  m.def(
      "apsi_receive",
      [](const std::string& config_pb,
         const std::shared_ptr<yacl::link::Context>& lctx) -> py::bytes {
        psi::ApsiReceiverConfig config;
        YACL_ENFORCE(config.ParseFromString(config_pb));

        auto r = psi::RunPir(config, lctx);
        return r.SerializeAsString();
      },
      py::arg("pir_config"), py::arg("link_context") = nullptr,
      "Run APSI receiver operations.");
}

PYBIND11_MODULE(libpsi, m) {
  py::register_exception_translator(
      [](std::exception_ptr p) {  // NOLINT: pybind11
        try {
          if (p) {
            std::rethrow_exception(p);
          }
        } catch (const yacl::Exception& e) {
          // Translate this exception to a standard RuntimeError
          PyErr_SetString(PyExc_RuntimeError,
                          fmt::format("what: \n\t{}\nstacktrace: \n{}\n",
                                      e.what(), e.stack_trace())
                              .c_str());
        }
      });

  py::module libs_m = m.def_submodule("libs");
  BindLibs(libs_m);

  py::module link_m = m.def_submodule("link");
  BindLink(link_m);

  m.def("_get_version", []() {
    return fmt::format("v{}.{}.{}{}", PSI_VERSION_MAJOR, PSI_VERSION_MINOR,
                       PSI_VERSION_PATCH, PSI_DEV_IDENTIFIER);
  });
}

}  // namespace psi
