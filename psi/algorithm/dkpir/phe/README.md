# EC-Elgamal Encryption
EC-Elgamal加密算法是一个基于椭圆曲线的半同态加密算法，它支持对密文进行同态加法运算。作为DkPir的组件，由于本场景中仅需要进行校验，故只实现了它的简化版本（加密，同态加法，校验）。
## 公共参数

- $E$ 表示椭圆曲线fourQ， $E$ 的阶为 $k$ 。
- $\mathrm{G}$ 为 $E$ 的生成元。

## 密钥生成

- 随机选取整数 $1 \le x < k$ ，令私钥为 $\mathrm{SK} = x$ , 公钥为 $\mathrm{PK} = x\mathrm{G}$ 。

## 加密
- 输入为公钥 $\mathrm{PK}$ 和明文 $m$ 。
- 随机选取整数 $1 \le r < k$ ，计算密文为 $\mathrm{C} = (c_0, c_1) = (r\mathrm{G}, m\mathrm{G} + r\mathrm{PK})$ 。

## 解密
- 输入为私钥 $\mathrm{SK} = x$ 和密文 $\mathrm{C}=(c_0, c_1)$ 。
- 先计算 $\mathrm{M} = c_1 - x c_0 = m\mathrm{G}$ ，再使用BSGS算法提取 $m$ 。
> **Note**<br>
解密性能较差，方案中并未使用。

## 校验
- 输入为私钥 $\mathrm{SK} = x$ ，明文 $m$ 和密文 $\mathrm{C}=(c_0, c_1)$ 。
- 为了判断密文 $\mathrm{C}$ 是否为明文 $m$ 的加密结果，仅需判断 $c_1 \overset{?}{=} m\mathrm{G} + xc_0$ 。
  
## 同态加法
- 假设使用同一公钥 $\mathrm{PK}$ 对明文 $m_1$ 和 $m_2$ 加密，分别得到密文 $\mathrm{C_1} = (c_{1,0},c_{1,1}) = (r_1\mathrm{G}, m_1\mathrm{G} + r_1\mathrm{PK})$ 和 $\mathrm{C_2} = (c_{2,0},c_{2,1}) = (r_2\mathrm{G}, m_2\mathrm{G} + r_2\mathrm{PK})$ 。
- 同态加法结果为 $\mathrm{C}' = (c_{1,0} + c_{2,0}, c_{1,1} + c_{2,1}) =((r_1 + r_2)\mathrm{G},(m_1 + m_2)\mathrm{G} + (r_1 + r_2)\mathrm{PK})$ ，这是 $m_1+m_2$ 对应的密文。

## 参考文献

- Chatzigiannis, P., Chalkias, K., & Nikolaenko, V. (2022). Homomorphic Decryption in Blockchains via Compressed Discrete-Log Lookup Tables. Lecture Notes in Computer Science (Including Subseries Lecture Notes in Artificial Intelligence and Lecture Notes in Bioinformatics), 13140 LNCS, 328–339. https://doi.org/10.1007/978-3-030-93944-1_23
- Bauer, J. (2004). Elliptic EcGroup Cryptography Tutorial. 1–13.
- Logarithms, D. (1976). a Public Key Cryptosystem and a Signature based on Discrete Logarithms. System, 10–18.