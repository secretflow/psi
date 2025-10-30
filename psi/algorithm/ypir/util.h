#pragma once

#include <cstddef>

#include "../spiral/params.h"
#include "../spiral/poly_matrix.h"

namespace psi::ypir {

psi::spiral::PolyMatrixNtt HomomorphicAutomorph(const psi::spiral::Params& params,
											 size_t t,
											 size_t t_exp,
											 const psi::spiral::PolyMatrixNtt& ct,
											 const psi::spiral::PolyMatrixNtt& pub_param);

// Ring packing (recursive inner) similar to pack_lwes_inner in reference.
psi::spiral::PolyMatrixNtt RingPackLwesInner(
	const psi::spiral::Params& params,
	size_t ell,
	size_t start_idx,
	const std::vector<psi::spiral::PolyMatrixNtt>& rlwe_cts,
	const std::vector<psi::spiral::PolyMatrixNtt>& pub_params,
	const std::pair<std::vector<psi::spiral::PolyMatrixNtt>, std::vector<psi::spiral::PolyMatrixNtt>>& y_constants);

}  // namespace psi::ypir


