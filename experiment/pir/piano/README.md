# Piano PIR: Single-Server Private Information Retrieval with Sublinear Computation

## Scheme Parameters and Notation

- $\kappa$: Statistical security parameter
- $\lambda$: Computational security parameter
- $\alpha(\kappa)$: Arbitrarily small super-constant function
- $n$: Database size
- $Q = \sqrt{n} \log \kappa \cdot \alpha(\kappa)$: Total number of queries

## Preprocessing Phase

### Client-Side Initialization

1. **Primary Table Generation**

   - Sample $M_1 = \sqrt{n} \log \kappa \cdot \alpha(\kappa)$ PRF keys, $\\{sk_1, \ldots, sk_{M_1}\\} \in \\{0,1\\}^{\lambda}$
   - Initialize parities $\\{p_1, \ldots, p_{M_1}\\}$ to zeros
2. **Backup Table Generation**

   - For each chunk $j \in \\{0, 1, \ldots, \sqrt{n} - 1\\}$:
      - Sample $M_2 = \log \kappa \cdot \alpha(\kappa)$ PRF keys $\\{sk_{j,1}, \ldots, sk_{j,M_2}\\}$
      - Initialize chunk-specific parities $\\{p_{j,1}, \ldots, p_{j,M_2}\\}$ to zeros

### Streaming Database Preprocessing

For each database chunk $DB[j \sqrt{n} : (j+1) \sqrt{n}]$:

- Update primary table parity: for $i \in [M_1]$, $p_i \leftarrow p_i \oplus DB[\text{Set}[sk_i](j)]$
- Store replacement entries: sample $M_2$ tuples $(r, DB[r])$ where $r$ is a random index from the current chunk
- Update backup table parity: for $i \in \\{0, 1, \ldots, \sqrt{n} - 1\\} \setminus \\{j\\}$ and $k \in [M_2]$, $p_{i,k} \leftarrow p_{i,k} \oplus DB[\text{Set}[sk_{i,k}](j)]$
- Delete current chunk from local storage

## Online Query Phase

Query Protocol for Index $x \in \\{0, 1, \ldots, n-1\\}$

1. **Query Execution**

   - Find primary table hint $T_i = ((sk_i, x^{\prime}), p_i)$ where $x \in \text{Set}(sk_i, x^{\prime})$
   - Locate chunk $j^* = \text{chunk}(x)$
   - Find first unused replacement entry $(r, DB[r])$
   - Send set $S' = S \setminus \\{j^* \to r\\}$ to server
   - Server returns $q = \bigoplus_{k \in S'} DB[k]$
   - Compute answer $\beta = q \oplus p_i \oplus DB[r]$
2. **Table Refresh Mechanism**

   - Locate next unused backup entry $(sk_{j^*,k}, p_{j^*,k})$
   - If no entry exists, generate random $sk_{j^*,k}$ with $p_{j^*,k} = 0$
   - Update primary table with new entry: $((sk_{j^*,k}, x), p_{j^*,k} \oplus \beta)$

## Theoretical Guarantees

- **Client Storage**: $O(\sqrt{n})$
- **Amortized Server Computation**: $O(\sqrt{n})$
- **Amortized Communication Overhead**: $O(\sqrt{n})$
- **Query Complexity**: $O(\sqrt{n})$

## References

- **Paper**: [Piano PIR](https://eprint.iacr.org/2023/452)
- **Implementation**: [GitHub Repository](https://github.com/wuwuz/Piano-PIR-new)
