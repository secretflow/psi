# Efficient Linear Multiparty PSI and Extension to Circuit/Quorum PSI

(https://www.xueshufan.com/publication/3150904314)

&emsp;&emsp;Our protocols for all three problem settings, namely, mPSI, circuit PSI and qPSI, broadly have two phases. At a high level, in the first phase, a fixed designated party, say $P_1$, interacts with all other parties $P_2,...,P_n$ using 2-party protocols. In the second phase, all parties engage in 𝑛-party protocols to compute a circuit to get the requisite output.<br>
**multiparty_psi**<br>
&emsp;&emsp;For mPSI, in the first phase, we invoke a two-party functionality, which we call weak private set membership (wPSM) functionality,
between a leader, $P_1$ and each $P_i$ (for $i ∈ {2, · · · , 𝑛}$). Informally, the wPSM functionality, when invoked between $P_1$ and $P_i$ on their individual private sets does the following: for each element in $P_1$’s set, it outputs the same random value to both $P_1$ and $P_i$ , if that element is in $P_i$’s set, and outputs independent random values, otherwise. By invoking only n instances ofthe wPSM functionality overall, we ensure that the total communication complexity of this phase is linear in n. <br>
&emsp;&emsp;In the second phase, all the parties together run a secure multiparty computation to obtain shares of 0 for each element in $P_1$’s set that is in the intersection and shares of a random element for other elements.<br>
**circuit_psi**<br>
&emsp;&emsp;For circuit psi, the first phase additionally includes conversion ofthe outputs from the wPSM functionality to arithmetic shares of 1 if $P_1$ and $P_i$ received the same random value, and shares of 0, otherwise.<br>
&emsp;&emsp;In the second phase, for every element of $P_1$, all parties must get shares of 1 if that element belongs to the intersection, and shares of 0, otherwise.<br>
**quorum_psi**<br>
&emsp;&emsp;For qpsi, the first phase additionally includes conversion ofthe outputs from the wPSM functionality to arithmetic shares of 1 if $P_1$ and $P_i$ received the same random value, and shares of 0, otherwise.<br>
&emsp;&emsp;In the second phase, we appropriately choose another polynomial such that for each element in $P_1$’s set, the polynomial evaluates to 0 if and only if that element belongs to the quorum intersection, and random otherwise.<br>