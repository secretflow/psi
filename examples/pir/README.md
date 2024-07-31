# Keyword PIR(APSI) Examples

## Prepare Data

1. Generate test data.

```bash
python examples/pir/apsi/test_data_creator.py 100000 1 1 16

mv db.csv /tmp/db.csv
mv query.csv /tmp/query.csv

cp examples/pir/apsi/parameters/100K-1-16.json /tmp/100K-1-16.json
```

**NOTE**
1. We distinguish between labeled mode and unlabeled mode based on the number of columns in the db.csv file. If db.csv has 2 columns, the first column represents the key and the second column represents the value, and labeled mode is automatically enabled. Otherwise, it's unlabeled mode, which can be considered a non-balanced PSI.
2. In APSI, selecting appropriate parameters for databases of different scales is a challenging task. If you aim for optimal performance, seek support from professionals.




## Sender Setup Stage

At sender terminal, run

```bash
./bazel-bin/psi/main --config $(pwd)/examples/pir/config/apsi_sender_setup.json
```

**NOTE: Failure is possible. You may just retry.**

## Online stage.

At sender terminal, run

```bash
./bazel-bin/psi/main --config $(pwd)/examples/pir/config/apsi_sender_online.json
```

At receiver terminal, run

```bash
./bazel-bin/psi/main --config $(pwd)/examples/pir/config/apsi_receiver.json
```

## Run Server with Full Mode (No Seperate Setup Stage)

At sender terminal, run

```bash
./bazel-bin/psi/main --config $(pwd)/examples/pir/config/apsi_sender_full.json
```

At receiver terminal, run

```bash
./bazel-bin/psi/main --config $(pwd)/examples/pir/config/apsi_receiver.json
```

## Advanced Topic : Bucketized Sender DB

**Please note that to support very large databases, such as those exceeding one billion rows, we offer the Bucketized Sender DB mode. However, this mode may result in reduced indistinguishability and may involve minimal leakage in server data distribution. Please evaluate whether the bucketing mode is suitable for your use case based on these considerations.**


### Sender Setup Stage

At sender terminal, run

```bash
mkdir -p /tmp/apsi_sender_bucket/

./bazel-bin/psi/main --config $(pwd)/examples/pir/config/apsi_sender_setup_bucket.json
```

**NOTE: Failure is possible. You may just retry.**

### Online stage.

At sender terminal, run

```bash
./bazel-bin/psi/main --config $(pwd)/examples/pir/config/apsi_sender_online_bucket.json
```

At receiver terminal, run

```bash
./bazel-bin/psi/main --config $(pwd)/examples/pir/config/apsi_receiver_bucket.json
```
