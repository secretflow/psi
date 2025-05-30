Getting started
===============

Welcome to SecretFlow PSI Library. There are multiple methods to use PSI/PIR.

* C++ binaries, you could build the binary or with release docker image: `secretflow/release-ci:latest` (`secretflow/release-ci-aarch64:latest` for ARM).
* Python packages

    * `SPU <https://pypi.org/project/spu/>`_ warps the library as Python bindings. You could call PSI/PIR with spu.
    * `SecretFlow <https://pypi.org/project/secretflow/>`_ warps SPU further with user-friendly APIs.


* Applications

    * `SCQL <https://www.secretflow.org.cn/en/docs/scql/main/>`_ integrates this library to do JOIN operations.
    * `SecretPad <https://www.secretflow.org.cn/docs/quickstart/mvp-platform>`_ provides PSI component.


For `PSI`, we have a developing :doc:`v2 PSI <reference/psi_v2_config>`, and we recommend using it.

+------------------------+------------------------------------------------+---------------------------------------------+
|                        | PSI v1 APIs(Deprecated)                        | PSI v2 APIs                                 |
+========================+================================================+=============================================+
| Supported Protocols    | ECDH, KKRT, ECDH_OPRF_UB, DP_PSI, RR22         | ECDH, KKRT, RR22, ECDH_OPRF_UB              |
+------------------------+------------------------------------------------+---------------------------------------------+
| CSV parser             | Support a subset of csv files.                 | Apache Arrow, support all legal csv files.  |
+------------------------+------------------------------------------------+---------------------------------------------+
| Recovery after failure | Unsupported                                    | Supported                                   |
+------------------------+------------------------------------------------+---------------------------------------------+
| Support duplicated keys| Unsupported                                    | Supported                                   |
+------------------------+------------------------------------------------+---------------------------------------------+
| Release Docker         | Not provided                                   | Provided                                    |
+------------------------+------------------------------------------------+---------------------------------------------+
| Python Binding         | with SPU                                       | with SPU                                    |
+------------------------+------------------------------------------------+---------------------------------------------+


Installation
------------

C++ binaries
^^^^^^^^^^^^

Release Docker
""""""""""""""

Please check official release docker image at `dockerhub <https://hub.docker.com/r/secretflow/psi-anolis8>`_. We also have mirrors at Alibaba Cloud: `secretflow-registry.cn-hangzhou.cr.aliyuncs.com/secretflow/psi-anolis8`.


Building from Source
""""""""""""""""""""

Please see :ref:`building`.


Python packages
^^^^^^^^^^^^^^^

SPU
"""

Please check `SPU Installation Guidelines <https://www.secretflow.org.cn/en/docs/spu/main/getting_started/install>`_.

APIs: https://www.secretflow.org.cn/en/docs/spu/main/reference/py_api

SecretFlow
""""""""""

Please check `SecretFlow Installation page <https://www.secretflow.org.cn/en/docs/secretflow/main/getting_started/installation>`_.

APIs:

- PSI v1:
    - (Deprecated) https://www.secretflow.org.cn/en/docs/secretflow/main/source/secretflow#secretflow.SPU.psi_df
    - (Deprecated) https://www.secretflow.org.cn/en/docs/secretflow/main/source/secretflow#secretflow.SPU.psi_csv
    - (Deprecated) https://www.secretflow.org.cn/en/docs/secretflow/main/source/secretflow#secretflow.SPU.psi_join_df
    - (Deprecated) https://www.secretflow.org.cn/en/docs/secretflow/main/source/secretflow#secretflow.SPU.psi_join_csv

- PSI v2:
    - https://www.secretflow.org.cn/en/docs/secretflow/main/source/secretflow#secretflow.SPU.psi_v2

- PIR:
    - (Deleted) https://www.secretflow.org.cn/en/docs/secretflow/main/source/secretflow#secretflow.SPU.pir_setup
    - (Deleted) https://www.secretflow.org.cn/en/docs/secretflow/main/source/secretflow#secretflow.SPU.pir_query
    - (Deleted) https://www.secretflow.org.cn/en/docs/secretflow/main/source/secretflow#secretflow.SPU.pir_memory_query
    - PIR Configuration: https://www.secretflow.org.cn/en/docs/psi/v0.5.0b0/reference/pir_config

- Component: https://www.secretflow.org.cn/en/docs/secretflow/main/component/comp_list#psi

Applications
^^^^^^^^^^^^

SCQL
""""

Please check `SCQL Quickstart tutorial <https://www.secretflow.org.cn/en/docs/scql/main/intro/tutorial>`_.

Featured operators using PSI:
    - https://www.secretflow.org.cn/en/docs/scql/main/reference/operators#in
    - https://www.secretflow.org.cn/en/docs/scql/main/reference/operators#join

SecretPad
"""""""""

Please check `SecretPad handbook <https://www.secretflow.org.cn/docs/quickstart/mvp-platform>`_.

(Deprecated) Easy PSI
""""""""""""""""""""""""

Please check `Easy PSI handbook <https://www.secretflow.org.cn/zh-CN/docs/easy-psi>`_.


.. _building:

Building
--------

System Setup
^^^^^^^^^^^^

Dev Docker
""""""""""

You can use docker to compile::

    ## start container
    docker run -d -it --name psi-dev-$(whoami) \
         --mount type=bind,source="$(pwd)",target=/home/admin/dev/ \
         -w /home/admin/dev \
         --cap-add=SYS_PTRACE --security-opt seccomp=unconfined \
         --cap-add=NET_ADMIN \
         --privileged=true \
         --entrypoint="bash" \
         secretflow/release-ci:latest


    # attach to build container
    docker exec -it psi-dev-$(whoami) bash

Linux
""""""

You need to install:

* gcc>=11.2
* cmake>=3.26
* ninja
* nasm>=2.15
* python>=3.8
* bazel
* golang
* xxd
* lld
* perl>=5.20.3.1

For bazel, please check version in `.bazeliskrc <https://github.com/secretflow/psi/blob/main/.bazeliskrc>`_ or use bazelisk instead.

Build & UnitTest
^^^^^^^^^^^^^^^^

We use bazel for building and testing::

    # build as debug
    bazel build //... -c dbg

    # build as release
    bazel build //... -c opt

    # test
    bazel test //...

Reporting an Issue
------------------

Please create an issue at `Github Issues <https://github.com/secretflow/psi/issues>`_.

We will look into issues and get back to you soon.
