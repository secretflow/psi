# 带阈值的非平衡PSI方案

## 应用场景

带阈值PSI，指对于PSI任务，支持用户设置最大的交集量级（阈值），并满足用户侧获取的交集量级=min(设置的阈值，实际的交集量)。与此同时，双方不能获得超出阈值的那部分交集（假设实际交集100w，阈值为10w，则双方无法获知剩余90w交集的具体内容）。本方案是对隐语非平衡PSI方案进行的功能拓展。

## 方案简述

假设服务端 $P_1$ 拥有大小为 $n_1$ 的集合 $X$ ，客户端 $P_2$ 拥有大小为 $n_2$ 的集合 $Y$ ，其中 $n_1 \gg n_2$ 。

### 离线阶段

$P_1$ 随机选取 $\alpha \gets \mathbb{Z}_q$ ，对于 $1 \le i \le n_1$ ，计算 $tx_i = H_2(H_1(x_i)^\alpha)$ 并将集合 $TX = \{tx_1,tx_2,...,tx_{n_1}\}$ 在shuffle后发送给  $P_2$ 。

### 在线阶段

1. 对于 $1 \le j \le n_2$ ， $P_2$ 随机选取 $\beta \gets \mathbb{Z}_q$ ，计算 $a_j = H_1(y_j)^{\beta}$ ，并将集合 $\{a_1,a_2,...,a_{n_2}\}$ 发送给 $P_1$ 。
2. 对于 $1 \le j \le n_2$ ， $P_1$ 计算 $a_j' = ({a_j})^{\alpha}$ ，将集合 $\{a_1',a_2',...,a'_{n_2} \}$ shuffle后发送给 $P_2$ 。
3. 对于 $1 \le j \le n_2$ ， $P_2$ 计算 $ty_j=H_2(({a_j'})^{1/{\beta}})$ ， 得到集合 $TY=\{{ty}_1,{ty}_2,...,{ty}_{n_2}\}$ 。 $P_2$ 可根据阈值对交集 $TY \cap TX$ 进行截取，并将截断后的索引集合发送给 $P_1$ 。
4. $P_1$ 根据第二步中的shuffle关系还原索引集合，并将还原后的索引集合发送给 $P_2$ 。
5. $P_2$ 通过还原后的索引集合获取交集。

## 全流程测试

1. 编译

    ```bash
    bazel build //psi/apps/psi_launcher:main
    ```

2. 生成测试数据

    ```bash
    $ python examples/psi/generate_psi_data.py --receiver_item_cnt 1e3 \
        --sender_item_cnt 1e6 --intersection_cnt 1e2 --id_cnt 2 \
        --receiver_path /temp/client_input.csv \
        --sender_path /temp/server_input.csv \
        --intersection_path /temp/intersection.csv
    ```

3. 服务端生成私钥

    ```bash
    openssl rand 32 > /temp/server_secret_key.key
    ```

4. 运行PSI

    在服务端终端，运行

    ```bash
    ./bazel-bin/psi/apps/psi_launcher/main --config $(pwd)/examples/psi/config/threshold_ub_psi_server_full.json
    ```

    在客户端终端，运行

    ```bash
    ./bazel-bin/psi/apps/psi_launcher/main --config $(pwd)/examples/psi/config/threshold_ub_psi_client_full.json
    ```

