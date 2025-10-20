# Threshold ECDH Private Set Intersection

## Introduction

Threshold Private Set Intersection (Threshold PSI) allows users to set an intersection threshold to limit the size of the intersection. Specifically, the final intersection size is the smaller count between the threshold and the actual intersection size. Additionally, both parties are restricted from accessing any part of the intersection beyond the defined threshold. This protocol is a functional extension based on ECDH PSI.

## Threshold ECDH PSI

$P_1$ and $P_2$ are the participants in the protocol, where $P_1$ is the receiver and $P_2$ is the sender. $X$ and $Y$ are the respective input sets of $P_1$ and $P_2$ , with size $n_1 = \left\vert X \right\vert$ and $n_2 = \left\vert Y \right\vert$.

1. For $1 \le i \le n_1$ , $P_1$ randomly chooses $\alpha \gets Z_q$ , computes $a_i = H(x_i)^{\alpha}$ , and sends the set $\\{a_1,a_2,...,a_{n_1}\\}$ to $P_2$ .
2. For $1 \le i \le n_1$ , $P_2$ randomly chooses $\beta \gets Z_q$ , computes $a_i' = (H(x_i)^{\alpha})^{\beta}$ . Then it samples a random permutation $\pi$ and sends the permuted set $TX = \pi(\\{a_1',a_2',...,a'_{n_1}\\})$ to $P_1$.
3. For $1 \le j \le n_2$ , $P_2$ computes $b_j = H(y_j)^{\beta}$ , and sends the set $\\{b_1,b_2,...,b_{n_2}\\}$ to $P_1$ .
4. For $1 \le j \le n_2$ , $P_1$ computes $b_j' = (H(y_j)^{\beta})^{\alpha}$ and gets the set $TY = \\{b_1',b_2',...,b'_{n_2}\\}$ .
5. $P_1$ truncates the intersection $TX \cap TY$ based on the threshold and sends the truncated index set to $P_2$ .
6. $P_2$ restores the index set based on the permutation $\pi$ from the second step and sends the restored index set to $P_1$ .
7. $P_1$ obtains the intersection through the restored index set.

## Test

1. Compile the binary

    ```bash
    bazel build //psi/apps/psi_launcher:main
    ```

2. Generate test data

    ```bash
    $ python examples/psi/generate_psi_data.py --receiver_item_cnt 1e3 \
        --sender_item_cnt 1e3 --intersection_cnt 1e2 --id_cnt 2 \
        --receiver_path /tmp/receiver_input.csv \
        --sender_path /tmp/sender_input.csv \
        --intersection_path /tmp/intersection.csv
    ```

3. Launch Threshold ECDH PSI

    For sender terminal,

    ```bash
    ./bazel-bin/psi/apps/psi_launcher/main --config $(pwd)/examples/psi/config/threshold_ecdh_psi_sender.json
    ```

    For receiver terminal,

    ```bash
    ./bazel-bin/psi/apps/psi_launcher/main --config $(pwd)/examples/psi/config/threshold_ecdh_psi_receiver.json
    ```
