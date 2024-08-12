# PSI V2 Benchmark

This document will introduce the PSI V2 Benchmark. It uses the PSI V2 version of the interface.
## Building from source

```
git clone https://github.com/secretflow/psi.git
cd psi
bazel build //... -c opt
```

If the building is successful, you will find an executable file named `main` in the 'bazel-bin/psi' directory. We will use `./main` file, combined with config file to run different PSI protocols. Such as:

```
./main --config configs/sender.config
./main --config configs/receiver.config

```
## Generate Data


In order to measure the performance of different PSI protocols under different data scales, we need to generate dummy data through [generate_psi.py](https://github.com/secretflow/secretflow/blob/main/docs/developer/benchmark/resources/generate_psi.py)

```python
python3 generate_psi.py 10000000 10000000

python3 generate_psi.py 100000000 100000000
```

## Prepare config file

We use the config file to specify different PSI protocols and input data.

### sender.config
```
{
         "psi_config": {
             "protocol_config": {
                 "protocol": "PROTOCOL_KKRT",
                 "role": "ROLE_SENDER",
                 "broadcast_result": false
             },
             "input_config": {
                 "type": "IO_TYPE_FILE_CSV",
                 "path": "/home/zuoxian/v2psi/datas/psi_1_1kw.csv"
             },
             "output_config": {
                 "type": "IO_TYPE_FILE_CSV",
                 "path": "/home/zuoxian/v2psi/sender/sender_output.csv"
             },
             "keys": [
                 "id",
             ],
             "debug_options": {
                 "trace_path": "/home/zuoxian/v2psi/sender/sender.trace"
             },
             "skip_duplicates_check": false,
             "disable_alignment": false,
             "advanced_join_type": "ADVANCED_JOIN_TYPE_UNSPECIFIED",
             "left_side": "ROLE_RECEIVER",
             "check_hash_digest": false,
             "recovery_config": {
                 "enabled": false
             }
         },
         "link_config": {
             "parties": [
                 {
                     "id": "receiver",
                     "host": "127.0.0.1:5300"
                 },
                 {
                     "id": "sender",
                     "host": "127.0.0.1:5400"
                 }
             ]
         },
         "self_link_party": "sender"
     }
```


### receiver.config
```
{
         "psi_config": {
             "protocol_config": {
                 "protocol": "PROTOCOL_KKRT",
                 "role": "ROLE_RECEIVER",
                 "broadcast_result": false
             },
             "input_config": {
                 "type": "IO_TYPE_FILE_CSV",
                 "path": "/home/zuoxian/v2psi/datas/psi_2_1kw.csv"
             },
             "output_config": {
                 "type": "IO_TYPE_FILE_CSV",
                 "path": "/home/zuoxian/v2psi/receiver/receiver_output.csv"
             },
             "keys": [
                 "id",
             ],
             "debug_options": {
                 "trace_path": "/home/zuoxian/v2psi/receiver/receiver.trace"
             },
             "skip_duplicates_check": false,
             "disable_alignment": false,
             "advanced_join_type": "ADVANCED_JOIN_TYPE_UNSPECIFIED",
             "left_side": "ROLE_RECEIVER",
             "check_hash_digest": false,
             "recovery_config": {
                 "enabled": false
             }
         },
         "link_config": {
             "parties": [
                 {
                     "id": "receiver",
                     "host": "127.0.0.1:5300"
                 },
                 {
                     "id": "sender",
                     "host": "127.0.0.1:5400"
                 }
             ]
         },
         "self_link_party": "receiver"
     }
```


## Run PSI with docker

In order to measure the PSI V2 benchmark under different machine configurations and network configurations, we use two dockers to act as sender and receiver respectively.


**alice**

```
docker run -d -it --name "alice" \
         --mount type=bind,source="$(pwd)",target=/home/admin/dev/ \
         -w /home/admin/dev \
         --cap-add=SYS_PTRACE --security-opt seccomp=unconfined \
         --cap-add=NET_ADMIN \
         --cpuset-cpus 0-31 \
         --privileged=true \
         --memory="64g" \
         --entrypoint="bash" \
         secretflow/ubuntu-base-ci:latest

docker start alice
docker exec -it alice bash
```


**bob**

```
docker run -d -it --name "bob" \
         --mount type=bind,source="$(pwd)",target=/home/admin/dev/ \
         -w /home/admin/dev \
         --cap-add=SYS_PTRACE --security-opt seccomp=unconfined \
         --cap-add=NET_ADMIN \
         --cpuset-cpus 32-63 \
         --privileged=true \
         --memory="64g" \
         --entrypoint="bash" \
         secretflow/ubuntu-base-ci:latest

docker start bob
docker exec -it bob bash
```

## Limit bandwidth and latency

```
BAND_WIDTH=100
burst=128
DELAY=10
netem_limit=8000

tc qdisc add dev eth0 root handle 1: tbf rate ${BAND_WIDTH}mbit burst ${burst}kb latency 800ms
tc qdisc add dev eth0 parent 1:1 handle 10: netem delay ${DELAY}msec limit ${netem_limit}
```


## Benchmark

Here we show the PSI V2 Benchmark measured as above.
> The default time unit is seconds, `m` represents minutes, and `h` represents hours.

<table>
  <tr>
    <th>Machine Configuration</th>
    <th>Algorithm parameters</th>
    <th>Protocol</th>
    <th>Network Configuration</th>
    <th>10 million~10 million</th>
    <th>100 million to 100 million</th>
    <th>1 billion to 1 billion</th>
  </tr>
  <tr>
    <td rowspan="10">32C64G</td>
    <td rowspan="2">receiver='alice',<br>protocol='ECDH_PSI_2PC',<br>curve_type='CURVE_FOURQ',<br>precheck_input=False,<br>sort=False,<br>broadcast_result=False,</td>
    <td rowspan="2">ECDH-PSI-2PC<br/>（FourQ）</td>
    <td>LAN</td>
    <td>42</td>
    <td>459</td>
    <td>1.45 h</td>
  </tr>
  <tr>
    <td>100Mbps/10ms</td>
    <td>63</td>
    <td>559</td>
    <td>1.72 h</td>
  </tr>
  <tr>
    <td rowspan="2">receiver='alice',<br>protocol='ECDH_PSI_2PC',<br>curve_type='CURVE_25519',<br>precheck_input=False,<br>sort=False,<br>broadcast_result=False,</td>
    <td rowspan="2">ECDH-PSI-2PC<br/>（CURVE_25519）</td>
    <td>LAN</td>
    <td>67</td>
    <td>697</td>
    <td>2.09 h</td>
  </tr>
  <tr>
    <td>100Mbps/10ms</td>
    <td>69</td>
    <td>669</td>
    <td>2.08 h</td>
  </tr>
  <tr>
    <td rowspan="2">receiver='alice',<br>protocol='KKRT_PSI_2PC',<br>precheck_input=False,<br>sort=False,<br>broadcast_result=False,</td>
    <td rowspan="2">KKRT_PSI_2PC<br/>（BUCKET SIZE IS ONE MILLION）</td>
    <td>LAN</td>
    <td>38</td>
    <td>357</td>
    <td>1.13 h</td>
  </tr>
  <tr>
    <td>100Mbps/10ms</td>
    <td>125</td>
    <td>1258</td>
    <td>3.53 h</td>
  </tr>
  <tr>
    <td rowspan="2">receiver='alice',<br>protocol='RR22_FAST_PSI_2PC',<br>precheck_input=False,<br>sort=False,<br>broadcast_result=False,</td>
    <td rowspan="2">RR22_FAST_PSI<br/>_2PC<br/>（BUCKET SIZE IS ONE MILLION）</td>
    <td>LAN</td>
    <td>17</td>
    <td>225</td>
    <td>0.62 h</td>
  </tr>
  <tr>
    <td>100Mbps/10ms</td>
    <td>44</td>
    <td>452</td>
    <td>1.43 h</td>
  </tr>
  <tr>
    <td rowspan="2">receiver='alice',<br>protocol='RR22_LOWCOMM_PSI_2PC',<br>precheck_input=False,<br>sort=False,<br>broadcast_result=False,</td>
    <td rowspan="2">RR22_LOWCOMM<br/>_PSI_2PC<br/>（BUCKET SIZE IS ONE MILLION）</td>
    <td>LAN</td>
    <td>21</td>
    <td>223</td>
    <td>0.75 h</td>
  </tr>
  <tr>
    <td>100Mbps/10ms</td>
    <td>38</td>
    <td>388</td>
    <td>1.21 h</td>
  </tr>





  <tr>
    <td rowspan="10">16C32G</td>
    <td rowspan="2">receiver='alice',<br>protocol='ECDH_PSI_2PC',<br>curve_type='CURVE_FOURQ',<br>precheck_input=False,<br>sort=False,<br>broadcast_result=False,</td>
    <td rowspan="2">ECDH-PSI-2PC<br/>（FourQ）</td>
    <td>LAN</td>
    <td>46</td>
    <td>497</td>
    <td>1.66 h</td>
  </tr>
  <tr>
    <td>100Mbps/10ms</td>
    <td>62</td>
    <td>574</td>
    <td>2.00 h</td>
  </tr>
  <tr>
    <td rowspan="2">receiver='alice',<br>protocol='ECDH_PSI_2PC',<br>curve_type='CURVE_25519',<br>precheck_input=False,<br>sort=False,<br>broadcast_result=False,</td>
    <td rowspan="2">ECDH-PSI-2PC<br/>（CURVE_25519）</td>
    <td>LAN</td>
    <td>69</td>
    <td>701</td>
    <td>2.23 h</td>
  </tr>
  <tr>
    <td>100Mbps/10ms</td>
    <td>74</td>
    <td>744</td>
    <td>2.49 h</td>
  </tr>
  <tr>
    <td rowspan="2">receiver='alice',<br>protocol='KKRT_PSI_2PC',<br>precheck_input=False,<br>sort=False,<br>broadcast_result=False,</td>
    <td rowspan="2">KKRT_PSI_2PC<br/>（BUCKET SIZE IS ONE MILLION）</td>
    <td>LAN</td>
    <td>37</td>
    <td>352</td>
    <td>1.18 h</td>
  </tr>
  <tr>
    <td>100Mbps/10ms</td>
    <td>124</td>
    <td>1228</td>
    <td>3.69 h</td>
  </tr>
  <tr>
    <td rowspan="2">receiver='alice',<br>protocol='RR22_FAST_PSI_2PC',<br>precheck_input=False,<br>sort=False,<br>broadcast_result=False,</td>
    <td rowspan="2">RR22_FAST_PSI<br/>_2PC<br/>（BUCKET SIZE IS ONE MILLION）</td>
    <td>LAN</td>
    <td>17</td>
    <td>178</td>
    <td>0.72 h</td>
  </tr>
  <tr>
    <td>100Mbps/10ms</td>
    <td>43</td>
    <td>452</td>
    <td>1.70 h</td>
  </tr>
  <tr>
    <td rowspan="2">receiver='alice',<br>protocol='RR22_LOWCOMM_PSI_2PC',<br>precheck_input=False,<br>sort=False,<br>broadcast_result=False,</td>
    <td rowspan="2">RR22_LOWCOMM<br/>_PSI_2PC<br/>（BUCKET SIZE IS ONE MILLION）</td>
    <td>LAN</td>
    <td>23</td>
    <td>216</td>
    <td>0.92 h</td>
  </tr>
  <tr>
    <td>100Mbps/10ms</td>
    <td>40</td>
    <td>384</td>
    <td>1.52 h</td>
  </tr>

  <tr>
    <td rowspan="10">8C16G</td>
    <td rowspan="2">receiver='alice',<br>protocol='ECDH_PSI_2PC',<br>curve_type='CURVE_FOURQ',<br>precheck_input=False,<br>sort=False,<br>broadcast_result=False,</td>
    <td rowspan="2">ECDH-PSI-2PC<br/>（FourQ）</td>
    <td>LAN</td>
    <td>66</td>
    <td>669</td>
    <td>2.6 h</td>
  </tr>
  <tr>
    <td>100Mbps/10ms</td>
    <td>66</td>
    <td>666</td>
    <td>2.6 h</td>
  </tr>
  <tr>
    <td rowspan="2">receiver='alice',<br>protocol='ECDH_PSI_2PC',<br>curve_type='CURVE_25519',<br>precheck_input=False,<br>sort=False,<br>broadcast_result=False,</td>
    <td rowspan="2">ECDH-PSI-2PC<br/>（CURVE_25519）</td>
    <td>LAN</td>
    <td>114</td>
    <td>1225</td>
    <td>3.8 h</td>
  </tr>
  <tr>
    <td>100Mbps/10ms</td>
    <td>112</td>
    <td>1124</td>
    <td>3.8 h</td>
  </tr>
  <tr>
    <td rowspan="2">receiver='alice',<br>protocol='KKRT_PSI_2PC',<br>precheck_input=False,<br>sort=False,<br>broadcast_result=False,</td>
    <td rowspan="2">KKRT_PSI_2PC<br/>（BUCKET SIZE IS ONE MILLION）</td>
    <td>LAN</td>
    <td>37</td>
    <td>351</td>
    <td>1.6 h</td>
  </tr>
  <tr>
    <td>100Mbps/10ms</td>
    <td>124</td>
    <td>1211</td>
    <td>4.0 h</td>
  </tr>
  <tr>
    <td rowspan="2">receiver='alice',<br>protocol='RR22_FAST_PSI_2PC',<br>precheck_input=False,<br>sort=False,<br>broadcast_result=False,</td>
    <td rowspan="2">RR22_FAST_PSI<br/>_2PC<br/>（BUCKET SIZE IS ONE MILLION）</td>
    <td>LAN</td>
    <td>19</td>
    <td>166</td>
    <td>1.08 h</td>
  </tr>
  <tr>
    <td>100Mbps/10ms</td>
    <td>44</td>
    <td>440</td>
    <td>1.91 h</td>
  </tr>
  <tr>
    <td rowspan="2">receiver='alice',<br>protocol='RR22_LOWCOMM_PSI_2PC',<br>precheck_input=False,<br>sort=False,<br>broadcast_result=False,</td>
    <td rowspan="2">RR22_LOWCOMM<br/>_PSI_2PC<br/>（BUCKET SIZE IS ONE MILLION）</td>
    <td>LAN</td>
    <td>22</td>
    <td>204</td>
    <td>1.17 h</td>
  </tr>
  <tr>
    <td>100Mbps/10ms</td>
    <td>37</td>
    <td>374</td>
    <td>1.72 h</td>
  </tr>
</table>
