# Demos

> All the commands should run at the root of the repo.

## 2P PSI

### ECDH

1. Compile the binary

```bash
$ bazel build //psi/psi:main -c opt
```

2. Generate mock data

```bash
$ python psi/psi/demo/data/test_data_generator.py --reciver_item_cnt 1e6 --sender_item_cnt 1e6 --intersection_cnt 8e4 --id_cnt 2 --receiver_path /tmp/receiver_input.csv --sender_path /tmp/sender_input.csv --intersection_path /tmp/intersection.csv
```

3. Run PSI

In two terminals,

For **receiver** terminal,

```bash
$ ./bazel-bin/psi/psi/main --config $(pwd)/psi/psi/demo/config/ecdh_receiver.json
```

For **sender** terminal,

```bash
$ ./bazel-bin/psi/psi/main --config $(pwd)/psi/psi/demo/config/ecdh_sender.json
```

4. Verify results.

```bash

# compare outputs between receiver and sender.
$ cmp --silent /tmp/receiver_output.csv /tmp/sender_output.csv && echo '### SUCCESS: Outputs between receiver and sender are identical! ###' || echo '### WARNING: Outputs between receiver and sender are different! ###'

# check whether receiver output is correct.
$ cmp --silent /tmp/receiver_output.csv /tmp/intersection.csv && echo '### SUCCESS: Receiver output and intersection are identical! ###' || echo '### WARNING: Receiver output and intersection are different! ###'
```