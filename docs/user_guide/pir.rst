PIR QuickStart
===============

Quick start with SPU Private Information Retrival (PIR).

Supported Protocols
-------------------

+---------------+--------------+---------------+
| PIR protocols | Type         | Server Number |
+===============+==============+===============+
| SealPIR       | IndexPIR     | Single Server |
+---------------+--------------+---------------+
| Labeled PS    |KeywordPIR    | Single Server |
+---------------+--------------+---------------+


Run keyword PIR c++ example
---------------------------

First build pir examples.

.. code-block:: bash

  bazel build //examples/pir/... -c opt

setup phase
>>>>>>>>>>>

Generate test usage oprf_key.bin

.. code-block:: bash

    dd if=/dev/urandom of=oprf_key.bin bs=32 count=1

Start server's terminal.

.. code-block:: bash

  ./bazel-bin/examples/pir/keyword_pir_setup -in_path <pir_server_data.csv> /
      -key_columns id -label_columns label -count_per_query 256 -max_label_length 40 /
      -oprf_key_path oprf_key.bin -setup_path pir_setup_dir

query phase
>>>>>>>>>>>

Start two terminals.

In the server's terminal.

.. code-block:: bash

  ./bazel-bin/examples/pir/keyword_pir_server -rank 0 -setup_path pir_setup_dir  -oprf_key_path oprf_key.bin


In the client's terminal.

.. code-block:: bash

  ./bazel-bin/examples/pir/keyword_pir_client -rank 1 -in_path <pir_client_data.csv> -key_columns id -out_path pir_out.csv

PIR query results write to pir_out.csv.
Run examples on two host, Please add '-parties ip1:port1,ip2:port2'.

Run keyword PIR python example
------------------------------

TODO

