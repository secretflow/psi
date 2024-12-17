# APSI Benchmark
This document introduces the APSI Benchmark.

## Building from source
```bash
git clone -b v0.4.3.dev240919 git@github.com:secretflow/psi.git
cd psi
bazel build //psi:main -c opt
```

If the build is successful, you will find an executable file named `main` in the `bazel-bin/psi` directory. We will use `./main` file, combined with a config file, to run APSI protocol. For exampple:

```plain
./main --config $(pwd)/examples/pir/config/apsi_sender_setup.json
```

## Generate Data
To measure the performance of APSI protocols under different data scales, we need to generate dummy data.


```python
# one million key-value pairs, each value's length is 32-byte, 
python examples/pir/apsi/test_data_creator.py --sender_size=1000000 --receiver_size=1 --intersection_size=1 --label_byte_count=32
# 16 million key-value pairs, each value's length is 32-byte, 
python examples/pir/apsi/test_data_creator.py --sender_size=16000000 --receiver_size=1 --intersection_size=1 --label_byte_count=32
```


Next, we need to copy the data and parameter files into /tmp:

```bash
cp db.csv /tmp/db.csv
cp query.csv /tmp/query.csv
cp examples/pir/apsi/parameters/1M-1-32.json /tmp/1M-1-32.json
# when our data scale is 16 million, we use 16M-1-32.json as our APSI parameter.
cp examples/pir/apsi/parameters/16M-1-32.json /tmp/16M-1-32.json
```



Please note that to achieve optimal APSI performance, we need to find a suitable set of parameters for the corresponding data scale and label length. Here, we directly use a set of default parameters provided by APSI, such as `1M-1-32.json`.



## Prepare config file
We use the config file to specify input data and parameter files.

### apsi_sender_setup.json
Note that for different data scales, we need to use different `params_file`.


```plain
{
  "apsi_sender_config": {
    "source_file": "/tmp/db.csv",
    "params_file": "/tmp/16M-1-32.json",
    "sdb_out_file": "/tmp/sdb",
    "save_db_only": true,
   "log_level": "info"
  }
}
```

### apsi_sender_online.json
```plain
{
    "apsi_sender_config": {
        "db_file": "/tmp/sdb",
        "log_level": "info"
    },
    "link_config": {
        "parties": [
            {
                "id": "sender",
                "host": "172.17.0.5:5300"
            },
            {
                "id": "receiver",
                "host": "172.17.0.6:5400"
            }
        ]
    },
    "self_link_party": "sender"
}
```

### apsi_receiver.json
Note that for different data scales, we need to use different `params_file`.

```plain
{
    "apsi_receiver_config": {
        "query_file": "/tmp/query.csv",
        "output_file": "/tmp/result.csv",
        "params_file": "/tmp/16M-1-32.json",
        "log_level": "info"
    },
    "link_config": {
        "parties": [
            {
                "id": "sender",
                "host": "172.17.0.5:5300"
            },
            {
                "id": "receiver",
                "host": "172.17.0.6:5400"
            }
        ]
    },
    "self_link_party": "receiver"
}
```



## Run APSI with docker
To measure the APSI benchmark under different machine and network configurations, we use two Docker containers to act as sender and receiver, respectively.

### **apsi_sender（32C64G）**
```plain
docker run -d -it --name "apsi_sender" \
         --mount type=bind,source="$(pwd)",target=/home/admin/dev/ \
         --mount type=bind,source=/tmp,target=/tmp \
         -w /home/admin/dev \
         --cap-add=SYS_PTRACE --security-opt seccomp=unconfined \
         --cap-add=NET_ADMIN \
         --cpuset-cpus 0-31 \
         --privileged=true \
         --memory="64g" \
         --entrypoint="bash" \
         secretflow/ubuntu-base-ci:latest

docker start apsi_sender
docker exec -it apsi_sender bash
```

Then run: 

```bash
# offline 
./main --config $(pwd)/examples/pir/config/apsi_sender_setup.json
# online
./main --config $(pwd)/examples/pir/config/apsi_sender_online.json
```

### apsi_receiver（16C32G）
```plain
docker run -d -it --name "apsi_receiver" \
         --mount type=bind,source="$(pwd)",target=/home/admin/dev/:z \
         -w /home/admin/dev \
         --cap-add=SYS_PTRACE --security-opt seccomp=unconfined \
         --cap-add=NET_ADMIN \
         --cpuset-cpus 32-47 \
         --privileged=true \
         --memory="32g" \
         --entrypoint="bash" \
         secretflow/ubuntu-base-ci:latest

docker start apsi_receiver
docker exec -it apsi_receiver bash
```

Then run:

```bash
./main --config $(pwd)/examples/pir/config/apsi_receiver.json
```

## Limit bandwidth and latency
```plain
BAND_WIDTH=100
burst=128
DELAY=10
netem_limit=8000

tc qdisc add dev eth0 root handle 1: tbf rate ${BAND_WIDTH}mbit burst ${burst}kb latency 800ms
tc qdisc add dev eth0 parent 1:1 handle 10: netem delay ${DELAY}msec limit ${netem_limit}
```

## Benchmark
Here we present the APSI Benchmark measured as described above. Please note that we do not record the time taken during the server-side setup phase, as this process can always be completed offline.

Additionally, only a small amount of performance data under specific data settings is presented here. This is because performance testing for APSI is quite complex; different data scales and label lengths require finding a corresponding set of parameters to achieve optimal performance. Therefore, the data settings here are intended to provide you with a rough reference.

If you wish to measure the APSI performance for a specific data scale and label length, you can follow the steps outlined above to reproduce the results. Finally, to further optimize performance, a good understanding of the APSI algorithm principles is necessary. If you have any further inquiries related to PIR, please feel free to contact us.

> `ms` represents millisecond.

<table>
    <tr>
        <th>DataBase Scale</th>
        <th>NetWork Configuration</th>
        <th>Client One Query Time</th>
    </tr>
    <tr>
        <td rowspan="2"><strong>one million（32-byte）</strong></td>
        <td>LAN</td>
        <td>494 ms</td>
    </tr>
    <tr>
        <td>100Mbps/10ms</td>
        <td>800 ms</td>
    </tr>
    <tr>
        <td rowspan="2"><strong>16 million（32-byte）</strong></td>
        <td>LAN</td>
        <td>2670 ms</td>
    </tr>
    <tr>
        <td>100Mbps/10ms</td>
        <td>3353 ms</td>
    </tr>
</table>




Note that the above data does not represent the optimal performance of APSI. Under fixed data scale conditions, the query performance of APSI is highly correlated with the corresponding parameters. Additionally, if you want to support larger datasets, such as one billion data entries, we also offer a bucket mode. However, this mode requires consideration of more parameters, so it is not displayed in this benchmark.


