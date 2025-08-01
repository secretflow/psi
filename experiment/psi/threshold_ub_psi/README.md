# Threshold Unbalanced Private Set Intersection

## Introduction

Threshold Private Set Intersection (Threshold PSI) allows users to set an intersection threshold to limit the size of the intersection. Specifically, the final intersection size is the smaller count between the threshold and the actual intersection size. Additionally, both parties are restricted from accessing any part of the intersection beyond the defined threshold. This protocol is a functional extension based on Unbalanced PSI.

## Threshold UB PSI

$P_1$ and $P_2$ are the participants in the protocol, where $P_1$ is the server and $P_2$ is the client. $X$ and $Y$ are the respective input sets of $P_1$ and $P_2$ , with size $n_1 = \left\vert X \right\vert$ and $n_2 = \left\vert Y \right\vert$ , where $n_1 \gg n_2$ .

### Offline Phase

$P_1$ randomly chooses $\alpha \gets Z_q$ . For $1 \le i \le n_1$ , $P_1$ compute $tx_i = H_2(H_1(x_i)^\alpha)$ and send the set $TX = \\{tx_1,tx_2,...,tx_{n_1}\\}$ to $P_2$ in shuffled order.

### Online Phase

1. For $1 \le j \le n_2$ , $P_2$ randomly chooses $\beta \gets Z_q$ , compute $a_j = H_1(y_j)^{\beta}$ , and send the set $\\{a_1,a_2,...,a_{n_2}\\}$ to $P_1$ .
2. For $1 \le j \le n_2$ , $P_1$ computes $a_j' = ({a_j})^{\alpha}$ . Then it samples a random permutation $\pi$ and send the permuted set $\pi(\\{a_1',a_2',...,a'_{n_2}\\})$ to $P_2$.
3. For $1 \le j \le n_2$ , $P_2$ computes $ty_j = H_2(({a_j'})^{1/{\beta}})$ and gets the set $TY = \\{ ty_1,ty_2 ,..., ty_{n_2} \\}$ . $P_2$ can truncate the intersection $TY \cap TX$ based on the threshold and send the truncated index set to $P_1$ .
4. $P_1$ restores the index set based on the permutation $\pi$ from the second step and sends the restored index set to $P_2$ .
5. $P_2$ obtains the intersection through the restored index set.

## Test

1. Compile the binary

    ```bash
    bazel build //psi/apps/psi_launcher:main
    ```

2. Generate test data

    ```bash
    $ python examples/psi/generate_psi_data.py --receiver_item_cnt 1e3 \
        --sender_item_cnt 1e6 --intersection_cnt 1e2 --id_cnt 2 \
        --receiver_path /tmp/client_input.csv \
        --sender_path /tmp/server_input.csv \
        --intersection_path /tmp/intersection.csv
    ```

3. Generate secret key for server

    ```bash
    openssl rand 32 > /tmp/server_secret_key.key
    ```

4. Launch Threshold UB PSI

    For server terminal,

    ```bash
    ./bazel-bin/psi/apps/psi_launcher/main --config $(pwd)/examples/psi/config/threshold_ub_psi_server_full.json
    ```

    For client terminal,

    ```bash
    ./bazel-bin/psi/apps/psi_launcher/main --config $(pwd)/examples/psi/config/threshold_ub_psi_client_full.json
    ```
