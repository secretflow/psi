# PPS-Based PIR Implementation

## Overview

This repository contains the implementation of the Single Bits and Multi-Bits Private Information Retrieval (PIR) algorithms,
which incorporate Puncturable Pseudorandom Sets (PPS) as described in the paper
"Private Information Retrieval with Sublinear Online Time" (<https://eprint.iacr.org/2019/1075.pdf>) by Henry Corrigan-Gibbs and Dmitry Kogan.
By utilizing PPS, these implementations achieve fast, sublinear-time database lookups without increasing the server-side storage requirements.
The use of PPS enables the system to efficiently manage the complexity of data retrieval while ensuring the privacy of the client's queries,
optimizing both Single Bits and Multi-Bits PIR functionalities.

## Implementation Details

This project only implements the dual-server model described in the paper. The implementation of Puncturable Pseudorandom Sets is based on
Chapter 2 of the paper, "Construction 4 (Puncturable pseudorandom set from puncturable PRF)." The implementation of the Single-Bits PIR protocol
follows "Construction 16 (Two-server PIR with sublinear online time)" from Chapter 3 of the original paper.
The implementation of the Multi-Bits PIR protocol is based on Chapter 4 and Appendix D's "Construction 44 (Multi-query offline/online PIR)."

**(Offline/online PIR)**. An offline/online PIR scheme is a tuple $\Pi$ = (**Setup**, **Hint**, **Query**, **Answer**, **Reconstruct**) of
five efficient algorithms:

* **Setup**$(1^{\lambda}, n) \to (ck, q_h)$,  a randomized algorithm that takes in security parameter $\lambda$ and database length $n$
and outputs a client key $ck$ and a hint request $q_h$.

* **Hint**$(x, q_h) \to h$,  a deterministic algorithm that takes in a database $x \in \{0, 1\}^n$ and a hint request $q_h$ and outputs a hint $h$.

* **Query**$(ck, i) \to h$,  a randomized algorithm that takes in the client’s key $ck$ and an index $i \in [n]$, and outputs a a query $q$.

* **Answer**$(q) \to a$,  a deterministic algorithm that takes as input a query $q$ and gets access to an oracle that:
  * takes as input an index $j \in [n]$, and
  * returns the $j$-th bit of the database $x_j ∈ \{0, 1\}$

    outputs an answer string $a$, and

* **Reconstruct**$(h, a) \to x_i$,  , a deterministic algorithm that takes as a hint $h$ and an answer $a$, and outputs a bit $x_i$.

### Puncturable pseudorandom sets

Puncturable pseudorandom sets are an extension of puncturable pseudorandom functions (PRFs). A typical pseudorandom function generates outputs that
are indistinguishable from random by any efficient algorithm, given only the outputs and not the secret key. When a PRF is punctured at a particular
point, it behaves like a normal PRF for all inputs except for the punctured point, for which the output or behavior is obscured or undefined.

Our implementation utilizes the AES pseudorandom generator (PRG) to construct a GGM tree-based pseudorandom function (PRF), and then employs
Construction 4 from the original paper to build the Puncturable Pseudorandom Set (PPS).

### Single-Bits PIR

Construction 16 (Two-server PIR with sublinear online time).

### Multi-Bits PIR

Construction 44 (Multi-query offline/online PIR).

### Contact

email: <yangw.ing@foxmail.com>
