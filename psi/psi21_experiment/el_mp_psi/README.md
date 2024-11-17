论文题目：Efficient Linear Multiparty PSI and Extension to Circuit/Quorum PSI

论文地址：https://www.xueshufan.com/publication/3150904314

方案概括：multiparty_psi<br>
1、参与者为$P_1$,...,$P_n$。选择$P_1$作为接收方，$P_2$,...,$P_n$为发送方。所用参与者共同产生三个hash函数$h_1,h_2,h_3$。$P_1$通过Cuchoo Hashing产生一个$Table_1$，$P_2,...,P_n$使用普通的三次hash产生$Table_2,...,Table_n$（就是将一个数x  hash三次放在表的三个位置上）。<br>
2、使用PSM协议，将$P_1$作为接收方发送$Table_1$，$P_2,...,P_n$为发送方发送$Table_2,...,Table_n$。<br>
3、结束时$P_1$对于表中的每一个位置的元素查询了n-2次，计作$y_{ij}$，其中ij代表在$P_i$的表中查询第j个元素，$P_2$,...,$P_n$获得$w_{ij}$。$P_1$计算$<a_j>_1=\sum_{i=2}^{n} -y_{ij}$,$P_2$,...,$P_n$计算$<a_j>_i=w_{ij}$,若查询到元素，可以看出来所有$<a>$加起来为0。<br>