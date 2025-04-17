# 可计费的1查N PIR方案

## 应用场景
这是一个支持批量查询的Keyword PIR方案，也称为Duplicate Key PIR (DkPir)。协议包含两方，服务端（发送方）拥有一个数据库，客户端（接收方）有一个查询集合。其中，服务端的数据库中存在同一个key对应多行数据的情况：
```bash
a. Index = 2, key=张三, label = value1
b. Index = 6, key=张三, label = value2
c. Index = 9, key=张三, label = value3
d. ......
```
目标：

1.客户端可以查询到 “张三” 对应的所有行的数据（对于其他查询值同理），同时不暴露自己的查询集合。

2.服务端需要按照查询的实际行数完成计费，但它无法感知到客户端具体查询了哪些数据，也不能完全信任来自客户端的查询总行数。因此需要让服务端能够对客户端提供的行数进行校验，以防客户端为了节省费用而作假。
## 方案简述

本方案主要基于APSI进行功能扩展，APSI的实现细节可参考[APSI](https://github.com/microsoft/APSI)。

### 离线阶段

1. 服务端对原始数据进行处理，包括列合并（多列标签合为一个标签，分隔符为0x1E）和行合并（多行标签合为一个标签，分隔符为0x1F），在此过程中，服务端可得到一个行列合并后的数据表和一个行数表。其中，行数表记录了每个key对应的重复行数。
2. 对于数据表，服务端按照APSI中的方法生成SendDB。对于行数表 $\{(key,count)\}$ ，服务端会先选取一个随机的线性函数 $p(x)=ax+b$ 和半同态加密 $\mathrm{PHE}$（EC-Elgamal）的一对公私钥，再将 $\{(key,\mathrm{PHE.Enc}(p(count)))\}$ 作为输入，也生成一个SendDB（记为SendCntDB）。其中，SendDB和SendCntDB共用同一个OPRF密钥。

### 在线阶段

假设客户端查询key的数量为 $n$ 。

1. 客户端提出OPRF查询，服务端在生成OPRF查询结果时，会对结果进行shuffle再发给客户端。在此过程中，服务端也会记录客户端具体查询的key的数量 $n$ 。
2. 与APSI类似，客户端会发起一次APSI查询，服务端在收到该查询后，会分别用于行数查询和数据查询。
3. 服务端会先返回行数的查询密文。客户端在处理后可得到 $n$ 个形如 $\mathrm{PHE.Enc}(p(count))$ 的密文，将这些密文求和，并将求和结果发给服务端。
4. 服务端收到求和密文后，才会将数据的查询密文发给客户端。
5. 客户端收到数据的查询密文后，可以还原出真实数据的标签。由于在第一步中服务端进行了shuffle，因此标签无法与key进行一一对应。但是标签里的数据是真实的，它可以计算行数（比如通过行分割符0x1F的数量确定）。客户端统计出查询到的总行数，并以明文形式将总行数发给服务端。
6. 服务端在第四步中收到的密文应为 $\mathrm{PHE.Enc}(a\cdot sum + n \cdot b)$ ，在第五步中收到的总行数为 $sum'$ 。根据Ec-Elgamal的特性，可以根据私钥校验 $\mathrm{PHE.Enc}(a\cdot sum + n \cdot b)$ 是否为 $a\cdot sum' + n \cdot b$ 的加密结果。校验通过后，服务端将shuffle关系发给客户端。
7. 客户端根据收到的shuffle关系，还原出数据查询结果。

> **Note**<br>
方案中行数校验通过的前提是客户端查询的所有key在服务端的数据库中都能查到，存在一定的局限性。在实际使用时可以考虑在PIR之前使用一次PSI。

## 全流程测试

1. 编译
```bash
bazel build //psi/apps/psi_launcher:main
```
2. 移动数据
```bash
mkdir /temp
cp examples/pir/apsi/data/duplicate_key_db.csv /temp/duplicate_key_db.csv
cp examples/pir/apsi/data/duplicate_key_query.csv /temp/duplicate_key_query.csv
cp examples/pir/apsi/parameters/100-1-300.json /temp/100-1-300.json
```

3. 离线阶段


在发送方终端，运行
```bash
./bazel-bin/psi/apps/psi_launcher/main --config $(pwd)/examples/pir/config/dk_pir_sender_offline.json
```
4. 在线阶段


在发送方终端，运行
```bash
./bazel-bin/psi/apps/psi_launcher/main --config $(pwd)/examples/pir/config/dk_pir_sender_online.json
```

在接收方终端，运行
```bash
./bazel-bin/psi/apps/psi_launcher/main --config $(pwd)/examples/pir/config/dk_pir_receiver_online.json
```
