Efficient Linear Multiparty PSI and Extension to Circuit/Quorum PSI

(https://www.xueshufan.com/publication/3150904314)

Our protocols for all three problem settings, namely, mPSI, circuit PSI and qPSI, broadly have two phases. At a high level, in the first phase, a fixed designated party, say P1, interacts with all other parties P2,...,Pn using 2-party protocols. In the second phase, all parties engage in n-party protocols to compute a circuit to get the requisite output.

**multiparty_psi**

For mPSI, in the first phase, we invoke a two-party functionality, which we call weak private set membership (wPSM) functionality,
between a leader, P1 and each Pi (for i ∈ {2, · · · , n}). Informally, the wPSM functionality, when invoked between P1 and Pi on their individual private sets does the following: for each element in P1’s set, it outputs the same random value to both P1 and Pi , if that element is in Pi’s set, and outputs independent random values, otherwise. By invoking only n instances ofthe wPSM functionality overall, we ensure that the total communication complexity of this phase is linear in n. In the second phase, all the parties together run a secure multiparty computation to obtain shares of 0 for each element in P1’s set that is in the intersection and shares of a random element for other elements.

**circuit_psi**

For circuit psi, the first phase additionally includes conversion ofthe outputs from the wPSM functionality to arithmetic shares of 1 if P1 and Pi received the same random value, and shares of 0, otherwise. In the second phase, for every element of P1, all parties must get shares of 1 if that element belongs to the intersection, and shares of 0, otherwise.

**quorum_psi**

For qpsi, the first phase additionally includes conversion ofthe outputs from the wPSM functionality to arithmetic shares of 1 if P1 and Pi received the same random value, and shares of 0, otherwise. In the second phase, we appropriately choose another polynomial such that for each element in P1’s set, the polynomial evaluates to 0 if and only if that element belongs to the quorum intersection, and random otherwise.