PSI v1 QuickStart
=================

Quick start with Private Set Intersection (PSI) V1 APIs.


Supported Protocols
----------------------

.. The :psi_code_host:`ECDH-PSI </psi/blob/master/psi/psi/core/ecdh_psi.h>` is favorable if the bandwidth is the bottleneck.
.. If the computing is the bottleneck, you should try the BaRK-OPRF based
.. PSI :psi_code_host:`KKRT-PSI API </psi/blob/master/psi/psi/core/kkrt_psi.h>`.

+---------------+--------------+--------------+--------------+
| PSI protocols | Threat Model | Party Number |  PsiTypeCode |
+===============+==============+==============+==============+
| ECDH-PSI      | Semi-Honest  | 2P           |   1          |
+---------------+--------------+--------------+--------------+
| ECDH-OPRF-PSI | Semi-Honest  | 2P           |   -          |
+---------------+--------------+--------------+--------------+
| `KKRT`_       | Semi-Honest  | 2P           |   2          |
+---------------+--------------+--------------+--------------+
| `PCG_PSI`_    | Semi-Honest  | 2P           |   3          |
+---------------+--------------+--------------+--------------+
| `Mini-PSI`_   | Malicious    | 2P           |   -          |
+---------------+--------------+--------------+--------------+
| `DP-PSI`_     | Semi-Honest  | 2P           |   -          |
+---------------+--------------+--------------+--------------+

MPC and PSI protocols are designed for specific Security model (or Threat Models).

Security model are widely considered to capture the capabilities of adversaries.
Adversaries of semi-honest model and malicious model are Semi-honest Adversary and
Malicious Adversary.

- `Semi-honest Adversary <https://wiki.mpcalliance.org/semi_honest_adversary.html>`_
- `Malicious Adversary <https://wiki.mpcalliance.org/malicious_adversary.html>`_

Semi-Honest PSI Must not be used in Malicious environment, may be attacked and leak information.

Our implementation of ECDH-PSI protocol supports multiple ECC curves:

- `Curve25519 <https://en.wikipedia.org/wiki/Curve25519>`_
- `Secp256k1 <https://en.bitcoin.it/wiki/Secp256k1>`_
- `FourQ <https://en.wikipedia.org/wiki/FourQ>`_
- `SM2 <https://www.cryptopp.com/wiki/SM2>`_

.. _PCG_PSI: https://eprint.iacr.org/2022/334.pdf
.. _KKRT: https://eprint.iacr.org/2016/799.pdf
.. _Mini-PSI: https://eprint.iacr.org/2021/1159.pdf
.. _DP-PSI: https://arxiv.org/pdf/2208.13249.pdf

Please check :doc:`/development/psi_protocol_intro` for details.

Prepare data and config
-----------------------

Please check details of configs at :doc:`/reference/psi_v1_config`.

To launch PSI, please check LaunchConfig at :doc:`/reference/launch_config` and fillin **runtime_config.legacy_psi_config**.

Release Docker
--------------

Check official release docker image at `dockerhub <https://hub.docker.com/r/secretflow/psi-anolis8>`_. We also have mirrors at Alibaba Cloud: secretflow-registry.cn-hangzhou.cr.aliyuncs.com/secretflow/psi-anolis8.

Run PSI
-------

In the first terminal, run the following command::

    docker run -it  --rm  --network host --mount type=bind,source=/tmp/receiver,target=/root/receiver -w /root  --cap-add=SYS_PTRACE --security-opt seccomp=unconfined --cap-add=NET_ADMIN --privileged=true secretflow-registry.cn-hangzhou.cr.aliyuncs.com/secretflow/psi-anolis8:0.1.0beta bash -c "./main --config receiver/receiver.config"


In the other terminal, run the following command simultaneously::

    docker run -it  --rm  --network host --mount type=bind,source=/tmp/sender,target=/root/sender -w /root  --cap-add=SYS_PTRACE --security-opt seccomp=unconfined --cap-add=NET_ADMIN --privileged=true secretflow-registry.cn-hangzhou.cr.aliyuncs.com/secretflow/psi-anolis8:0.1.0beta bash -c "./main --config sender/sender.config"


Building from source
--------------------

You could build psi binary with bazel::

    bazel build psi/psi:main -c opt


Then use binary with::

    ./bazel-bin/psi/psi/main --config <config JSON file path>

Benchmark
----------

benchmark result without data load time

ecdh-psi Benchmark
>>>>>>>>>>>>>>>>>>

:psi_code_host:`DH-PSI benchmark code </psi/blob/master/psi/psi/core/ecdh_psi_bench.cc>`

cpu limited by docker(--cpu)

+---------------------------+-----+---------+---------+----------+----------+----------+
| Intel(R) Xeon(R) Platinum | cpu |  2^20   |  2^21   |  2^22    |  2^23    |  2^24    |
+===========================+=====+=========+=========+==========+==========+==========+
|                           | 4c  | 40.181s | 81.227s | 163.509s | 330.466s | 666.807s |
|  8269CY CPU @ 2.50GHz     +-----+---------+---------+----------+----------+----------+
|                           | 8c  | 20.682s | 42.054s | 85.272s  | 173.836s | 354.842s |
|  with curve25519-donna    +-----+---------+---------+----------+----------+----------+
|                           | 16c | 11.639s | 23.670s | 48.965s  | 100.903s | 208.156s |
+---------------------------+-----+---------+---------+----------+----------+----------+

`ipp-crypto Multi-buffer Functions <https://www.intel.com/content/www/us/en/develop/documentation/ipp-crypto-reference/top/multi-buffer-cryptography-functions/montgomery-curve25519-elliptic-curve-functions.html>`_


+---------------------------+-----+--------+--------+---------+---------+----------+
| Intel(R) Xeon(R) Platinum | cpu | 2^20   | 2^21   | 2^22    | 2^23    |   2^24   |
+===========================+=====+========+========+=========+=========+==========+
|                           | 4c  | 7.37s  | 15.32s | 31.932s | 66.802s | 139.994s |
|  8369B CPU @ 2.70GHz      +-----+--------+--------+---------+---------+----------+
|                           | 8c  | 4.3s   | 9.095s | 18.919s | 40.828s | 87.649s  |
|  curve25519(ipp-crypto)   +-----+--------+--------+---------+---------+----------+
|                           | 16c | 2.921s | 6.081s | 13.186s | 29.614s | 65.186s  |
+---------------------------+-----+--------+--------+---------+---------+----------+

kkrt-psi Benchmark
>>>>>>>>>>>>>>>>>>>

All of our experiments use a single thread for each party.

If the bandwidth is enough, the upstream could try to perform multi-threading optimizations

bandwidth limited by `wondershaper <https://github.com/magnific0/wondershaper/>`_.

10Mbps = 10240Kbps, 100Mbps = 102400Kbps, 1000Mbps = 1024000Kbps

.. code-block:: bash

  wondershaper -a lo -u 10240

Intel(R) Xeon(R) Platinum 8269CY CPU @ 2.50GHz

+-----------+---------+---------+---------+---------+----------+
| bandwidth |  phase  |   2^18  |   2^20  |   2^22  |   2^24   |
+===========+=========+=========+=========+=========+==========+
|           | offline | 0.012s  | 0.012s  | 0.012s  | 0.014s   |
|    LAN    +---------+---------+---------+---------+----------+
|           | online  | 0.495s  | 2.474s  | 10.765s | 44.368s  |
+-----------+---------+---------+---------+---------+----------+
|           | offline | 0.012s  | 0.012s  | 0.024s  | 0.014s   |
|  100Mbps  +---------+---------+---------+---------+----------+
|           | online  | 2.694s  | 11.048s | 46.983s | 192.37s  |
+-----------+---------+---------+---------+---------+----------+
|           | offline | 0.016s  | 0.019s  | 0.0312s | 0.018s   |
|  10Mbps   +---------+---------+---------+---------+----------+
|           | online  | 25.434s | 100.68s | 415.94s | 1672.21s |
+-----------+---------+---------+---------+---------+----------+

bc22 pcg-psi Benchmark
>>>>>>>>>>>>>>>>>>>>>>

Intel(R) Xeon(R) Platinum 8269CY CPU @ 2.50GHz

+-----------+---------+---------+---------+----------+---------+---------+
| bandwidth |   2^18  |   2^20  |   2^21  |   2^22   |   2^23  |   2^24  |
+===========+=========+=========+=========+==========+=========+=========+
|    LAN    | 1.261s  | 2.191s  | 3.503s  | 6.51s    | 13.012s | 26.71s  |
+-----------+---------+---------+---------+----------+---------+---------+
|  100Mbps  | 2.417s  | 6.054s  | 11.314s | 21.864s  | 43.778s | 88.29s  |
+-----------+---------+---------+---------+----------+---------+---------+
|   10Mbps  | 18.826s | 50.038s | 96.516s | 186.097s | 369.84s | 737.71s |
+-----------+---------+---------+---------+----------+---------+---------+


Security Tips
-------------

Warning:  `KKRT16 <https://eprint.iacr.org/2016/799.pdf>`_ and
`BC22 PCG <https://eprint.iacr.org/2022/334.pdf>`_ are semi-honest PSI protocols,
and may be attacked in malicious model.
We recommend using KKRT16 and BC22_PCG PSI protocol as one-way PSI, i.e., one party gets the final intersection result.
