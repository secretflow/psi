PIR QuickStart
===============

Quick start with SPU Private Information Retrival (PIR).

Supported Protocols
-------------------

+----------------+-------------+---------------+
| PIR protocols  |    Type     | Server Number |
+================+=============+===============+
| SealPIR(later) | Index PIR   | Single Server |
+----------------+-------------+---------------+
| APSI           | Keyword PIR | Single Server |
+----------------+-------------+---------------+

At this moment, SealPIR is under development.


Release Docker
--------------

Check official release docker image at `dockerhub <https://hub.docker.com/r/secretflow/psi-anolis8>`_. We also have mirrors at Alibaba Cloud: `secretflow-registry.cn-hangzhou.cr.aliyuncs.com/secretflow/psi-anolis8`.


Keyword PIR (APSI)
------------------

Before Start
>>>>>>>>>>>>

We provide a simple wrapper for famous `APSI <https://github.com/microsoft/APSI>`_ library. Please read the `README <https://github.com/microsoft/APSI?tab=readme-ov-file#introduction>`_ of the repo carefully.
We are not going to discuss any content related to APSI further. 


Please check details of configs at :doc:`/reference/pir_config`. You are supposed to be aware of that we provided the **EXACT** the same API to APSI.
So you should read `APSI CLI arguments <https://github.com/microsoft/APSI?tab=readme-ov-file#command-line-interface-cli>`_ as well.

The extra features brought are:

1. Use Yacl Link as communication layer.
2. Experimental bucketized PIR.
3. Provide APIs for further integration.

If you want to try a similar CLI like APSI, you could compile the source code by

.. code-block:: bash

    bazel build psi/wrapper/apsi/cli:receiver

    bazel build psi/wrapper/apsi/cli:sender


And get CLI parameters like this: 


.. code-block:: bash

    ./bazel-bin/psi/wrapper/apsi/cli/sender --help

    ./bazel-bin/psi/wrapper/apsi/cli/receiver --help


Prepare data and config
>>>>>>>>>>>>>>>>>>>>>>>

For Senders (Servers), you must provide a input csv or a sender db file. An input csv file could be turned into a sender db file after setup.


CSV File
""""""""

The csv file should looks like

.. code-block:: None

    key,value
    Yb,Ii
    Kw,uO
    LA,Oc
    Fr,RM
    NG,vT
    KR,ui
    jL,oA
    eV,cX
    uu,LK

Please make sure:

- Since version **0.4.0b0**, headers line is required.
- The first row must be headers, only **key** and **value** are allowed.
- The **key** column must be items(keys)
- The **value** column must be labels(values), this column is optional.


APSI Params File
""""""""""""""""

We use the original APSI params. For details, please check `APSI PSIParams <https://github.com/microsoft/APSI?tab=readme-ov-file#psiparams>`_ .

For senders: An APSI params file must be provided with CSV files. If a sender db file is provided, the APSI params is not required and would be ignored.
For receivers: The APSI params file is optional. If not provided, receivers will ask for senders. If provided, please make sure receivers and senders share
the same APSI params file, otherwise error occurred.

It's not easy to find a suitable APSI params file. So APSI provides some examples at `APSI parameters <https://github.com/microsoft/APSI/tree/main/parameters`_.
We have a copy at `APSI parameters <blob/main/examples/pir/apsi/parameters>` as well.
You can refer to `here <https://github.com/microsoft/APSI?tab=readme-ov-file#loading-from-json>`_ to choose the appropriate parameter file.


To launch PIR, please check LaunchConfig at :doc:`/reference/launch_config`, and 
specific config: :ref:`sender config <ApsiSenderConfig>` and :ref:`receiver config <ApsiReceiverConfig>`.


PIR Config
""""""""""

Sender: Setup Stage. In this stage, sender generates sender db file with csv file. This stage is offline.
Since version **0.4.0b0**, the source csv file for db generating should be specified as **source_file**, and **db_file** 
is used to specify the generated db file.

.. code-block:: json
   :caption: apsi_sender_setup.json

    {
        "apsi_sender_config": {
            "source_file": "/tmp/db.csv",
            "params_file": "/tmp/1M-256-288.json",
            "compress" : true,
            "sdb_out_file": "/tmp/sdb"
        }
    }

Sender: Online stage. In this stage, sender generates responses to receivers' queries. This stage is online.

.. code-block::
   :caption: apsi_sender_online.json

    {
        "apsi_sender_config": {
            "compress" : true,
            "db_file": "/tmp/sdb",
        },
        "link_config": {
            "parties": [
                {
                    "id": "sender",
                    "host": "127.0.0.1:5300"
                },
                {
                    "id": "receiver",
                    "host": "127.0.0.1:5400"
                }
            ]
        },
        "self_link_party": "sender"
    }

Receiver: Online stage.

.. code-block:: json
   :caption: apsi_receiver.json

    {
        "apsi_receiver_config": {
            "query_file": "/tmp/query.csv",
            "output_file": "/tmp/result.csv",
            "params_file": "/tmp/1M-256-288.json"
        },
        "link_config": {
            "parties": [
                {
                    "id": "sender",
                    "host": "127.0.0.1:5300"
                },
                {
                    "id": "receiver",
                    "host": "127.0.0.1:5400"
                }
            ]
        },
        "self_link_party": "receiver"
    }

params_file field is optional. If not provided, receiver will ask sender for params. If provided, please make sure you provide the same one to sender's.


Full Examples
>>>>>>>>>>>>>

Please read `examples <https://github.com/secretflow/psi/tree/main/examples/pir/README.md>`_.
For more demo configs, you can check `here <https://github.com/secretflow/psi/tree/main/examples/pir/config>`_.


Bucketized Mode
>>>>>>>>>>>>>>>

Searching in a large sender db is costly. So we can split data into buckets.
The server knows which bucket the client is querying, but does not know which specific element:

In the setup stage, sender split data into buckets. Each bucket will generate a sender db.

In the online stage, receiver split query into subqueries. Each subquery only contains items residing in the same bucket.
When receivers sends a subquery to the sender, bucket idx is also provided.

For each subquery, sender only search in the corresponding sender db for specific bucket.

Assuming the query indindiscernibility degree is :math:`\text{ind_degree}`, and sender has :math:`\text{sender_data_rows}` rows,  
then the number of buckets should be :math:`\text{bucket_num} = \text{sender_data_rows} / \text{ind_degree}`, you can set :ref:`experimental_bucket_cnt <ApsiSenderConfig>` with :math:`\text{bucket_num}`.
If the client has :math:`\text{query_rows}` rows of data, the we know client has :math:`\text{query_rows_per_bucket} = \text{query_rows} / \text{bucket_num}` rows per bucket, 
we can select :math:`\text{ind_degree}`-:math:`\text{query_rows_per_bucket}`-xxx.json as :ref:`params_file <ApsiSenderConfig>`.
