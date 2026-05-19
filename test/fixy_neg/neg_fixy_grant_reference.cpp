// fixy_neg: IsGrantTag rejects a reference-qualified grant in the
// Grants pack — closes fixy-A4-033 (paired with cv-qualified
// fixture for distinct mismatch classes per HS14).
//
// HS14 fixture 2/2.  Reference-qualified grant types arise from
// `decltype(grant_returning_fn())` or auto-deduced reference
// returns.  Pre-A4-033, `IsGrantTag_v<G>` stripped the reference
// via `std::remove_cvref_t<G>` before checking `is_base_of` +
// `is_final`, silently accepting `affine&` as a valid grant tag.
//
// Distinct from neg_fixy_grant_cv_qualified.cpp: that fixture
// witnesses cv-only rejection (`const`/`volatile`); this fixture
// witnesses reference rejection.  Both flow through the SAME
// `is_same_v<G, remove_cvref_t<G>>` clause but cover orthogonal
// cv-vs-ref bug classes.
//
// Expected diagnostic: "AllGrantsWellFormed".

#include <crucible/fixy/Fn.h>

namespace fixy = crucible::fixy;
namespace gr   = crucible::fixy::grant;
using D        = crucible::fixy::dim::DimensionAxis;

template <D Axis>
using strict = gr::accept_default_strict_for<Axis>;

int main() {
    // lvalue-reference-qualified `affine` in the Usage slot — must
    // reject via AllGrantsWellFormed.  All 20 other axes engage
    // with their strict acceptance markers so the rejection is
    // uniquely attributable to the reference-qualified grant.
    auto bad = fixy::mint_fn<int,
        strict<D::Refinement>, gr::affine& /* reference-qualified */,
        strict<D::Effect>, strict<D::Security>, strict<D::Protocol>,
        strict<D::Lifetime>, strict<D::Provenance>, strict<D::Trust>,
        strict<D::Representation>, strict<D::Observability>,
        strict<D::Complexity>, strict<D::Precision>, strict<D::Space>,
        strict<D::Overflow>, strict<D::Mutation>, strict<D::Reentrancy>,
        strict<D::Size>, strict<D::Version>, strict<D::Staleness>,
        strict<D::Synchronization>, strict<D::Regime>>(42);
    (void)bad;
    return 0;
}
