#pragma once

#include <cstddef>

#include "../spiral/params.h"
#include "../spiral/poly_matrix.h"

#include "psi/algorithm/ypir/types.h"
namespace psi::ypir {

using namespace psi::spiral;
std::vector<uint64_t> ConcatHorizontal(
    const std::vector<std::vector<uint64_t>>& v_a, size_t a_rows,
    size_t a_cols);

void MatMulVecPacked(uint32_t* out, const uint32_t* a, const uint32_t* b,
                     size_t a_rows, size_t a_cols, size_t b_rows,
                     size_t b_cols);

PolyMatrixNtt HomomorphicAutomorph(const Params& params, size_t t, size_t t_exp,
                                   const PolyMatrixNtt& ct,
                                   const PolyMatrixNtt& pub_param);

PolyMatrixNtt RingPackLwesInner(
    const Params& params, size_t ell, size_t start_idx,
    const std::vector<PolyMatrixNtt>& rlwe_cts,
    const std::vector<PolyMatrixNtt>& pub_params,
    const std::pair<std::vector<PolyMatrixNtt>, std::vector<PolyMatrixNtt>>&
        y_constants);

std::pair<std::vector<PolyMatrixNtt>, std::vector<PolyMatrixNtt>> GenYConstants(
    const Params& params);

PolyMatrixNtt RingPackLwes(
    const Params& params, const std::vector<uint64_t>& b_values,
    const std::vector<PolyMatrixNtt>& rlwe_cts, size_t num_cts,
    const std::vector<PolyMatrixNtt>& pub_params,
    const std::pair<std::vector<PolyMatrixNtt>, std::vector<PolyMatrixNtt>>&
        y_constants);

std::vector<uint64_t> NegacyclicPerm(absl::Span<const uint64_t> a, size_t shift,
                                     uint64_t modulus);
std::vector<uint64_t> NegacyclicMatrix(absl::Span<const uint64_t> a,
                                       uint64_t modulus);
PolyMatrixNtt CondenseMatrix(const Params& params, const PolyMatrixNtt& a);
PolyMatrixNtt UncondenseMatrix(const Params& params, const PolyMatrixNtt& a);
PolyMatrixNtt PackUsingSingleWithOffset(
    const Params& params, const std::vector<PolyMatrixNtt>& pub_params,
    const std::vector<PolyMatrixNtt>& cts, size_t offset);

std::vector<PolyMatrixNtt> PrepPackLwes(const Params& params,
                                        absl::Span<const uint64_t> lwe_cts,
                                        size_t cols_to_do);

std::vector<std::vector<PolyMatrixNtt>> PrepPackManyLwes(
    const Params& params, absl::Span<const uint64_t> lwe_cts,
    size_t num_rlwe_outputs);

std::vector<std::vector<uint64_t>> GenerateAutomorphTablesBruteForce(
    const Params& params);

std::tuple<PolyMatrixNtt, std::vector<PolyMatrixNtt>,
           std::vector<std::vector<uint64_t>>>
PrecomputePack(const Params& params, size_t poly_len_log2,
               const std::vector<PolyMatrixNtt>& prepacked,
               const std::vector<PolyMatrixNtt>& pub_params,
               const std::pair<std::vector<PolyMatrixNtt>,
                               std::vector<PolyMatrixNtt>>& y_constants);
void FastAddIntoNoReduce(PolyMatrixNtt& res, const PolyMatrixNtt& a);
void MultiplyPolyAvx(uint64_t* res, const uint64_t* a, const uint64_t* b,
                     size_t len);
void ScalarMultiplyAvx(const Params& params, PolyMatrixNtt& res,
                       const PolyMatrixNtt& a, const PolyMatrixNtt& b);

void FastMultiplyNoReduce(const Params& params, PolyMatrixNtt& res,
                          const PolyMatrixNtt& a, const PolyMatrixNtt& b,
                          size_t start_inner_dim = 0);
void ApplyAutomorphNttRaw(const Params& params, const uint64_t* poly,
                          uint64_t* out, size_t t,
                          const std::vector<std::vector<size_t>>& tables);
void ApplyAutomorphNtt(const Params& params,
                       const std::vector<std::vector<size_t>>& tables,
                       const PolyMatrixNtt& mat, PolyMatrixNtt& res, size_t t);
void FastReduce(const Params& params, PolyMatrixNtt& res);
PolyMatrixNtt PackUsingPrecompVals(
    const Params& params,
    size_t ell,  // params.poly_len_log2
    absl::Span<const PolyMatrixNtt> pub_params,
    absl::Span<const uint64_t> b_values, const PolyMatrixNtt& precomp_res,
    absl::Span<const PolyMatrixNtt> precomp_vals,
    const std::vector<std::vector<size_t>>& precomp_tables,
    const std::pair<std::vector<PolyMatrixNtt>, std::vector<PolyMatrixNtt>>&
        y_constants);

std::vector<PolyMatrixNtt> PackManyLwes(
    const Params& params,
    const std::vector<std::vector<PolyMatrixNtt>>& prep_rlwe_cts,
    const Precomp& precomp, absl::Span<const uint64_t> b_values,
    size_t num_rlwe_outputs,
    const std::vector<PolyMatrixNtt>& pack_pub_params_row_1s,
    const std::pair<std::vector<PolyMatrixNtt>, std::vector<PolyMatrixNtt>>&
        y_constants);

PolyMatrixNtt GetRegevSample(const Params& params, const PolyMatrixRaw& sk_reg,
                             yacl::crypto::Prg<uint64_t>& rng,
                             yacl::crypto::Prg<uint64_t>& rng_pub);
PolyMatrixNtt GetFreshRegevPublicKey(const Params& params,
                                     const PolyMatrixRaw& sk_reg, size_t m,
                                     yacl::crypto::Prg<uint64_t>& rng,
                                     yacl::crypto::Prg<uint64_t>& rng_pub);
std::vector<PolyMatrixNtt> RawGenerateExpansionParams(
    const Params& params, const PolyMatrixRaw& sk_reg, size_t num_exp,
    size_t m_exp, yacl::crypto::Prg<uint64_t>& rng,
    yacl::crypto::Prg<uint64_t>& rng_pub);

template <typename T>
void FastBatchedDotProduct(const Params& params, uint64_t* c, const uint64_t* a,
                           size_t a_elems, const T* b_t, size_t b_rows,
                           size_t b_cols);

std::vector<PolyMatrixNtt> UnpackVecPm(const Params& params, size_t rows,
                                       size_t cols,
                                       absl::Span<const uint64_t> data);

std::vector<uint8_t> ModulusSwitch(const Params& params,
                                   const PolyMatrixRaw& poly_matrix,
                                   uint64_t q_prime_1, uint64_t q_prime_2);
}  // namespace psi::ypir