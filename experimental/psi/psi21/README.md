Efficient Linear Multiparty PSI and Extensions to Circuit/Quorum PSI

(https://eprint.iacr.org/2021/172.pdf)

**multiparty_psi**

For mPSI, in the first phase, we invoke a two-party functionality, which we call weak private set membership (wPSM) functionality,
between a leader, $P_1$ and each $P_i$ (for i ∈ {2, · · · , n}). Informally, the wPSM functionality, when invoked between $P_1$ and $P_i$ on their individual private sets does the following: for each element in $P_1$’s set, it outputs the same random value to both $P_1$ and $P_i$ , if that element is in $P_i$’s set, and outputs independent random values, otherwise. By invoking only n instances ofthe wPSM functionality overall, we ensure that the total communication complexity of this phase is linear in n. In the second phase, all the parties together run a secure multiparty computation to obtain shares of 0 for each element in $P_1$’s set that is in the intersection and shares of a random element for other elements.

Parameters: <br>
There are n parties $P_1,...,P_n$ with private sets of size m. Let 𝛽 = 1.28𝑚, 𝜎 = 𝜅 + ⌈log𝑚⌉ + 3 and 𝑝 > 2𝜎 is a prime. Additions and multiplications in the protocol are over the field $F_𝑝$. Let 𝑡 < 𝑛/2 be the corruption threshold.<br>
Input:<br>
Each party $P_i$ has input set $X_i = \{x_{i1},··· ,x_{im}\}$, where $x_{ij} ∈ \{0,1\}^𝜎$. Note that element size can always be made 𝜎 bits by first hashing the elements using an appropriate universal hash function.<br>
Protocol:<br>
(1) Pre-processing (Randomness generation required for Step (4)): $P_1,...,P_n$ compute $([𝑠_1],···,[𝑠_𝛽]) ← RandomF_{n,t}(𝛽)$.<br>
(2) Hashing: Parties agree on hash functions $h_1,h_2,h_3 : \{0,1\}^𝜎 → [𝛽]$. $P_1$ does stash-less cuckoo hashing on $X_1$ using $h_1,h_2,h_3$ to generate $Table_1$ and inserts dummy elements into empty bins. For $i ∈ \{2,··· ,n\}$, $P_i$ does simple hashing of $X_i$ using $h_1,h_2,h_3$ into $Table_i$, i.e., stores each $x ∈ X_i$ at locations $ℎ_1(𝑥),ℎ_2(𝑥) and ℎ_3(𝑥)$. If the three locations are not distinct, insert dummy elements in $Table_i$.<br>
(3) Invoking the wPSM functionality: For each $i ∈ \{2,··· ,n\}$ , $P_1$ and $P_i$ invoke the wPSM functionality for $𝑁 = 3𝑚$ as follows:<br>
 • $P_i$ is the sender with input $\{Table_𝑖[𝑗]\}_{𝑗∈[𝛽]}$ .<br>
 • $P_1$ is the receiver with input $\{Table_1[𝑗]\}_{𝑗∈[𝛽]}$ .<br>
 • $P_1$ receives the outputs $\{𝑦_{𝑖𝑗}\}_{𝑗∈[𝛽]}$ and 𝑃𝑖 receives $\{𝑤_{𝑖𝑗}\}_{𝑗∈[𝛽]}$ .<br>
 (4) Evaluation: For $j ∈ [𝛽]$,<br>
 • $P_1$ computes $<a_j>_1 = \sum (-y_{ij} mod p)$ and for $i ∈ \{2,...,n\}$, $P_i$ sets $<a_j>_i = (w_{ij} mod p)$ .<br>
 • $P_1,...,P_n$ compute $[𝑎_𝑗] ← ConvertShares_{𝑛,𝑡}(⟨𝑎_𝑗⟩)$ .<br>
 • $P_1,...,P_n$ invoke the following multiparty functionalities: $[v_j] ← MultF_{𝑛,𝑡}([a_j], [s_j]). v_j ← Reveal_{𝑛,𝑡}([v_j]).$<br>
 (5) Output: $P_1$ computes the intersection, permutes its elements and announces to all parties.
