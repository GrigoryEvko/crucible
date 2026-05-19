// fixy_neg: mint_fn rejects a non-grant type in the Grants pack.
//
// HS14 floor for FIXY-AUDIT-D2.  mint_fn's `requires IsAccepted`  // fixy-A4-023: post-H-05 rename.
// chains to `AllGrantsWellFormed<Grants...>` which requires every
// pack element to satisfy `grant::IsGrantTag`.  Threading `int`
// (a plain type, NOT a grant tag) through the pack fires the
// well-formedness gate before AllDimsEngaged runs.
//
// Expected diagnostic: "AllGrantsWellFormed" — the satisfaction-failure
// chain names the grant-well-formedness concept.

#include <crucible/fixy/Fn.h>

namespace fixy = crucible::fixy;
namespace gr   = crucible::fixy::grant;
using D        = crucible::fixy::dim::DimensionAxis;

template <D Axis>
using strict = gr::accept_default_strict_for<Axis>;

int main() {
    // 19-grant pack with `int` substituted for Usage — IsAccepted
    // must reject via AllGrantsWellFormed: int does not satisfy
    // grant::IsGrantTag.
    auto bad = fixy::mint_fn<int,
        strict<D::Refinement>, int /* non-grant in Usage slot */,
        strict<D::Effect>, strict<D::Security>, strict<D::Protocol>,
        strict<D::Lifetime>, strict<D::Provenance>, strict<D::Trust>,
        strict<D::Representation>, strict<D::Observability>,
        strict<D::Complexity>, strict<D::Precision>, strict<D::Space>,
        strict<D::Overflow>, strict<D::Mutation>, strict<D::Reentrancy>,
        strict<D::Size>, strict<D::Version>, strict<D::Staleness>, strict<D::Synchronization>, strict<D::Regime>>(42);
    (void)bad;
    return 0;
}
