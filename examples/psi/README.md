# Demos

> All the commands should run at **the root of the repo**.

## 2P PSI

1. Compile the binary

```bash
$ bazel build //psi:main -c opt
```

2. Generate test data

```bash
$ python examples/psi/generate_psi_data.py --receiver_item_cnt 1e6 \
    --sender_item_cnt 1e6 --intersection_cnt 8e4 --id_cnt 2 \
    --receiver_path /tmp/receiver_input.csv \
    --sender_path /tmp/sender_input.csv \
    --intersection_path /tmp/intersection.csv
```

3. Generate recovery cache folder

```bash
$ mkdir -p /tmp/ecdh_sender_cache
$ mkdir -p /tmp/ecdh_receiver_cache

```


4. Launch PSI

In two terminals,

For **receiver** terminal,

```bash
$ ./bazel-bin/psi/main --config $(pwd)/examples/psi/config/ecdh_receiver_recovery.json
```

For **sender** terminal,

```bash
$ ./bazel-bin/psi/main --config $(pwd)/examples/psi/config/ecdh_sender_recovery.json
```

## 2P UB PSI

1. Compile the binary

```bash
$ bazel build //psi:main -c opt
```

2. Generate test data

```bash
$ python examples/psi/generate_psi_data.py --receiver_item_cnt 1e3 \
    --sender_item_cnt 1e6 --intersection_cnt 1e2 --id_cnt 2 \
    --receiver_path /tmp/client_input.csv \
    --sender_path /tmp/server_input.csv \
    --intersection_path /tmp/intersection.csv
```

3. Generate Secret Key for Server

```bash
$ openssl rand 32 > /tmp/server_secret_key.key
```

4. Launch UB PSI

In two terminals,

For **server** terminal,

```bash
$ ./bazel-bin/psi/main --config $(pwd)/examples/psi/config/ecdh_server_full.json
```

For **client** terminal,

```bash
$ ./bazel-bin/psi/main --config $(pwd)/examples/psi/config/ecdh_client_full.json
```
