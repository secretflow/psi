#include "psi/algorithm/ypir/util.h"

#include <cstring>

#include "../spiral/gadget.h"
#include "../spiral/poly_matrix_utils.h"

namespace psi::ypir {

using namespace psi::spiral;

PolyMatrixNtt HomomorphicAutomorph(const Params& params, size_t t,
									   size_t t_exp, const PolyMatrixNtt& ct,
									   const PolyMatrixNtt& pub_param) {
	YACL_ENFORCE(ct.Rows() == static_cast<size_t>(2));
	YACL_ENFORCE(ct.Cols() == static_cast<size_t>(1));

	auto ct_raw = PolyMatrixRaw::Zero(params.PolyLen(), 2, 1);
	FromNtt(params, ct_raw, ct);
	auto ct_auto = Automorphism(params, ct_raw, t);

	auto ginv_ct = PolyMatrixRaw::Zero(params.PolyLen(), t_exp, 1);
	psi::spiral::util::GadgetInvertRdim(params, ginv_ct, ct_auto, 1);

	auto ginv_ct_ntt = PolyMatrixNtt::Zero(params.CrtCount(), params.PolyLen(), t_exp, 1);
	ToNttNoReduce(params, ginv_ct_ntt, ginv_ct);

	auto w_times_ginv_ct = Multiply(params, pub_param, ginv_ct_ntt);

	auto ct_auto_1 = PolyMatrixRaw::Zero(params.PolyLen(), 1, 1);

	std::memcpy(ct_auto_1.Data().data(),
	            ct_auto.Data().data() + ct_auto.PolyStartIndex(1, 0),
	            sizeof(uint64_t) * ct_auto.NumWords());
	auto ct_auto_1_ntt = ToNtt(params, ct_auto_1);

	auto res = Add(params, ct_auto_1_ntt.PadTop(1), w_times_ginv_ct);
	return res;
}

PolyMatrixNtt RingPackLwesInner(
	const Params& params,
	size_t ell,
	size_t start_idx,
	const std::vector<PolyMatrixNtt>& rlwe_cts,
	const std::vector<PolyMatrixNtt>& pub_params,
	const std::pair<std::vector<PolyMatrixNtt>, std::vector<PolyMatrixNtt>>& y_constants) {
	YACL_ENFORCE_EQ(pub_params.size(), params.PolyLenLog2());

	if (ell == 0) {
		return rlwe_cts[start_idx];
	}

	size_t step = 1ULL << (params.PolyLenLog2() - ell);
	size_t even = start_idx;
	size_t odd = start_idx + step;

	auto ct_even = RingPackLwesInner(params, ell - 1, even, rlwe_cts, pub_params, y_constants);
	auto ct_odd = RingPackLwesInner(params, ell - 1, odd, rlwe_cts, pub_params, y_constants);

	const auto& y = y_constants.first[ell - 1];
	const auto& neg_y = y_constants.second[ell - 1];

	auto y_times_ct_odd = ScalarMultiply(params, y, ct_odd);
	auto neg_y_times_ct_odd = ScalarMultiply(params, neg_y, ct_odd);

	auto ct_sum_1 = ct_even;
	AddInto(params, ct_sum_1, neg_y_times_ct_odd);
	AddInto(params, ct_even, y_times_ct_odd);

	size_t t = (1ULL << ell) + 1;
	const auto& pub_param = pub_params[params.PolyLenLog2() - 1 - (ell - 1)];
	auto ct_sum_1_automorphed = HomomorphicAutomorph(params, t, params.TExpLeft(), ct_sum_1, pub_param);

	return Add(params, ct_even, ct_sum_1_automorphed);
}

}  // namespace psi::ypir


