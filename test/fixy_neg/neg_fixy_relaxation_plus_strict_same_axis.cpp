// fixy_neg: IsAcceptedGrants rejects relaxation + strict marker on same
// axis.
//
// HS14 floor for FIXY-AUDIT-G4.  `UniqueEngagementPerAxis` must fire
// when a binding engages the SAME axis with BOTH a relaxation tag and
// a strict-default marker — a different code path than two strict
// markers (already covered by neg_fixy_duplicate_engagement) because
// the engagement-detection trait branches on tag kind.
//
// Here: Effect axis is engaged by `with_io` (a relaxation tag) AND
// `strict<D::Effect>` (an acceptance marker).  Each individually is
// well-formed; together they violate unique-engagement-per-axis.
//
// Why this matters: stance aliases are typedefs of `fn<Type, ...>`,
// so "stacking stances" via direct grant composition isn't typeable.
// The real failure mode authors hit is supplementing a stance's
// implicit relaxation by also writing the strict marker (e.g., "be
// explicit about Effect engagement"), which is exactly this case.
//
// Expected diagnostic: "UniqueEngagementPerAxis" — the satisfaction-
// failure chain names the multiplicity concept.

#include <crucible/fixy/Fn.h>

namespace fixy = crucible::fixy;
namespace gr   = crucible::fixy::grant;
using D        = crucible::fixy::dim::DimensionAxis;

template <D Axis>
using strict = gr::accept_default_strict_for<Axis>;

int main() {
    // 20-element pack: 19 distinct axes covered + strict<D::Effect>
    // collides with the with_io relaxation that already engages Effect.
    auto bad = fixy::mint_fn<int,
        strict<D::Refinement>, strict<D::Usage>,
        gr::with_io,            // Effect engagement #1 (relaxation)
        strict<D::Effect>,      // Effect engagement #2 (strict marker)
        strict<D::Security>, strict<D::Protocol>, strict<D::Lifetime>,
        strict<D::Provenance>, strict<D::Trust>, strict<D::Representation>,
        strict<D::Observability>, strict<D::Complexity>, strict<D::Precision>,
        strict<D::Space>, strict<D::Overflow>, strict<D::Mutation>,
        strict<D::Reentrancy>, strict<D::Size>, strict<D::Version>,
        strict<D::Staleness>, strict<D::Synchronization>>(42);
    (void)bad;
    return 0;
}
