PIR QuickStart
===============

Quick start with SPU Private Information Retrival (PIR).

Supported Protocols
-------------------

+---------------+---------------+---------------+
| PIR protocols | Type          | Server Number |
+===============+===============+===============+
| SealPIR       | Index PIR     | Single Server |
+---------------+---------------+---------------+
| APSI          | Keyword PIR   | Single Server |
+---------------+---------------+---------------+

At this moment, SealPIR is not available in public APIs.


Release Docker
--------------

Check official release docker image at `dockerhub <https://hub.docker.com/r/secretflow/psi-anolis8>`_. We also have mirrors at Alibaba Cloud: secretflow-registry.cn-hangzhou.cr.aliyuncs.com/secretflow/psi-anolis8.


Run keyword PIR example(APSI)
-----------------------------

Please check details of configs at :doc:`/reference/pir_config`.

To launch PIR, please check LaunchConfig at :doc:`/reference/launch_config` and fillin **runtime_config.pir_config**.


Prepare data and config
>>>>>>>>>>>>>>>>>>>>>>>


You need to prepare following files:

+--------------------------+------------------------------------------------+-------------------------------------------------------------------------------+
| File(Folder) Name        | Location                                       | Description                                                                   |
+==========================+================================================+===============================================================================+
| apsi_client.json         | /tmp/client/apsi_client.json                   | Config for PIR client.                                                        |
+--------------------------+------------------------------------------------+-------------------------------------------------------------------------------+
| apsi_server_setup.json   | /tmp/server/apsi_server_setup.json             | Config for PIR server setup stage.                                            |
+--------------------------+------------------------------------------------+-------------------------------------------------------------------------------+
| apsi_server_online.json  | /tmp/server/apsi_server_online                 | Config for PIR server online stage.                                           |
+--------------------------+------------------------------------------------+-------------------------------------------------------------------------------+
| pir_server_setup         | /tmp/server/pir_server_setup                   | Folder for PIR server setup files.                                            |
+--------------------------+------------------------------------------------+-------------------------------------------------------------------------------+
| server_secret_key.bin    | /tmp/server/server_secret_key.bin              | Secret key for PIR server.                                                    |
+--------------------------+------------------------------------------------+-------------------------------------------------------------------------------+
| pir_server.csv           | /tmp/server/pir_server.csv                     | Input for PIR server.                                                         |
+--------------------------+------------------------------------------------+-------------------------------------------------------------------------------+
| pir_client.csv           | /tmp/client/pir_client.csv                     | Input for PIR client.                                                         |
+--------------------------+------------------------------------------------+-------------------------------------------------------------------------------+




1. pir_server.csv and pir_client.csv

.. code-block:: bash

  bazel run //examples/pir:generate_pir_data -c opt -- -data_count 10000 -label_len 32 -server_out_path /tmp/pir_server.csv -client_out_path /tmp/pir_client.csv


The files looks like

.. code-block::
   :caption: pir_server.csv

    id,id1,label,label1
    0000000000900000000,111111,cad43884c86ccb2e9236d7bd6bff976e,a508c64755636d08a2d18516ea5d6448
    0000000001900000001,111111,058f29d20bcf7e85ba641e2b62ea4fd2,3ff60253b7e740ff49652836b366dd32
    0000000002900000002,111111,381776671df03957919a922b9a8b0bae,a249a8608d401ee9998a2d8f1e942595
    0000000003900000003,111111,85d7befe77ab793e5ec409cd5e808463,7fca0e741c7b4115bb93fc575a1c18fa
    0000000004900000004,111111,a4fc01b0e0a5c65c2c5b2af9dc1b70bc,934fb0334ab777471bb58009ad0b255b
    0000000005900000005,111111,abb8da0d4e1cf8b7952cbe321f5f4c63,a0e37158c9f9afe1a50563bfcf84bf4f
    0000000006900000006,111111,c1016b1bdd0521db256487bbd56d5c2e,14ba1f1624861f68a6eb8e3cbece53a1
    0000000007900000007,111111,b8453bb94b50c231df4bcfdb7b3be9a6,e6a677b128d8355dafafbe0f0c96d536
    0000000008900000008,111111,0cc97ec32e5768b67e25608724e7ccd8,3b3aaf41662e764a9baa9487ef20da6c


.. code-block::
   :caption: pir_client.csv

    id,id1
    0000000974900000974,111111
    0000002122900002122,111111
    0000003839900003839,111111
    0000004198900004198,111111
    0000004773900004773,111111
    0000006269900006269,111111
    0000006641900006641,111111
    0000006881900006881,111111
    0000007237900007237,111111

If source code is not available to you, you may create the file by yourself.


2. server_secret_key.bin

.. code-block:: bash

    dd if=/dev/urandom of=server_secret_key.bin bs=32 count=1


3. configs

.. code-block::
   :caption: apsi_client.json

    {
        "pir_config": {
            "mode": "MODE_CLIENT",
            "pir_protocol": "PIR_PROTOCOL_KEYWORD_PIR_APSI",
            "pir_client_config": {
                "input_path": "/root/client/pir_client.csv",
                "key_columns": [
                    "id"
                ],
                "output_path": "/root/client/pir_output.csv"
            }
        },
        "link_config": {
            "parties": [
                {
                    "id": "server",
                    "host": "127.0.0.1:5300"
                },
                {
                    "id": "client",
                    "host": "127.0.0.1:5400"
                }
            ]
        },
        "self_link_party": "client"
    }

.. code-block::
   :caption: apsi_server_setup.json

    {
        "pir_config": {
            "mode": "MODE_SERVER_SETUP",
            "pir_protocol": "PIR_PROTOCOL_KEYWORD_PIR_APSI",
            "pir_server_config": {
                "input_path": "/root/server/pir_server.csv",
                "setup_path": "/root/server/pir_server_setup",
                "key_columns": [
                    "id"
                ],
                "label_columns": [
                    "label"
                ],
                "label_max_len": 288,
                "bucket_size": 1000000,
                "apsi_server_config": {
                    "oprf_key_path": "/root/server/server_secret_key.bin",
                    "num_per_query": 1,
                    "compressed": false
                }
            }
        }
    }


.. code-block::
   :caption: apsi_server_online.json

    {
        "pir_config": {
            "mode": "MODE_SERVER_ONLINE",
            "pir_protocol": "PIR_PROTOCOL_KEYWORD_PIR_APSI",
            "pir_server_config": {
                "setup_path": "/root/server/pir_server_setup"
            }
        },
        "link_config": {
            "parties": [
                {
                    "id": "server",
                    "host": "127.0.0.1:5300"
                },
                {
                    "id": "client",
                    "host": "127.0.0.1:5400"
                }
            ]
        },
        "self_link_party": "server"
    }


Setup Phase
>>>>>>>>>>>

.. code-block:: bash

  docker run -it  --rm  --network host --mount type=bind,source=/tmp/server,target=/root/server --cap-add=SYS_PTRACE --security-opt seccomp=unconfined --cap-add=NET_ADMIN --privileged=true secretflow-registry.cn-hangzhou.cr.aliyuncs.com/secretflow/psi-anolis8:0.1.0beta --config server/apsi_server_setup.json

Online Phase
>>>>>>>>>>>>

Start two terminals.

In the server's terminal.

.. code-block:: bash

  docker run -it  --rm  --network host --mount type=bind,source=/tmp/server,target=/root/server --cap-add=SYS_PTRACE --security-opt seccomp=unconfined --cap-add=NET_ADMIN --privileged=true secretflow-registry.cn-hangzhou.cr.aliyuncs.com/secretflow/psi-anolis8:0.1.0beta --config server/apsi_server_online.json


In the client's terminal.

.. code-block:: bash

  docker run -it  --rm  --network host --mount type=bind,source=/tmp/client,target=/root/client --cap-add=SYS_PTRACE --security-opt seccomp=unconfined --cap-add=NET_ADMIN --privileged=true secretflow-registry.cn-hangzhou.cr.aliyuncs.com/secretflow/psi-anolis8:0.1.0beta --config client/apsi_client.json


More examples
-------------

Please read https://github.com/secretflow/psi/tree/main/examples/pir/README.md
Please check more demo configs at https://github.com/secretflow/psi/tree/main/examples/pir/config

Run keyword PIR python example
------------------------------

TODO

