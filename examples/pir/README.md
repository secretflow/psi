# PIR Examples

## Prepare Data

1. Generate test data.

```bash
bazel run //examples/pir:generate_pir_data -c opt -- -data_count 10000 -label_len 32 -server_out_path /tmp/pir_server.csv -client_out_path /tmp/pir_client.csv
```

## Server Setup Stage

1. Create server setup folder.

```bash
mkdir -p /tmp/pir_server_setup
```


2. Create test server key.

```bash
dd if=/dev/urandom of=/tmp/server_secret_key.bin bs=32 count=1
```

3. Run server setup stage.

```bash
./bazel-bin/psi/main --config $(pwd)/examples/pir/config/apsi_server_setup.json
```

## Online stage.

At server terminal, run

```bash
./bazel-bin/psi/main --config $(pwd)/examples/pir/config/apsi_server_online.json
```

At client terminal, run

```bash
./bazel-bin/psi/main --config $(pwd)/examples/pir/config/apsi_client.json
```

## Run Server with Full Mode (No Seperate Setup Stage)

At server terminal, run

```bash
./bazel-bin/psi/main --config $(pwd)/examples/pir/config/apsi_server_full.json
```

At client terminal, run

```bash
./bazel-bin/psi/main --config $(pwd)/examples/pir/config/apsi_client.json
```
