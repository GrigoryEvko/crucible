// fixy_neg: IsGrantTag rejects a cv-qualified grant in the Grants
// pack — closes fixy-A4-033.
//
// HS14 fixture 1/2.  Pre-A4-033, `IsGrantTag_v<G>` stripped cv-ref
// via `std::remove_cvref_t<G>` before checking `is_base_of` +
// `is_final`, silently accepting `const affine` as a valid grant.
// The only way to PRODUCE a `const affine` type is to copy-paste
// from a runtime variable's `decltype` (`const auto g = affine{};
// using G = decltype(g);`) — a code smell the type system should
// flag rather than coerce.
//
// Tightened form: `IsGrantTag_v<G>` adds an `is_same_v<G,
// remove_cvref_t<G>>` clause, rejecting `const affine` at the
// `AllGrantsWellFormed` gate.  The mint_fn requires-clause chain
// names `AllGrantsWellFormed` in the satisfaction-failure
// diagnostic.
//
// Expected diagnostic: "AllGrantsWellFormed".

#include <crucible/fixy/Fn.h>

namespace fixy = crucible::fixy;
namespace gr   = crucible::fixy::grant;
using D        = crucible::fixy::dim::DimensionAxis;

template <D Axis>
using strict = gr::accept_default_strict_for<Axis>;

int main() {
    // const-qualified `affine` in the Usage slot — must reject via
    // AllGrantsWellFormed.  All 20 other axes engage with their
    // strict acceptance markers so the rejection is uniquely
    // attributable to the cv-qualified grant.
    auto bad = fixy::mint_fn<int,
        strict<D::Refinement>, const gr::affine /* cv-qualified */,
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
