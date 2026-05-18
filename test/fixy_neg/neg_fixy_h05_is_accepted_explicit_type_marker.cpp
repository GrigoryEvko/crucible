// fixy_neg: wrapper-discipline IsAccepted rejects explicit user Type-marker.
//
// HS14 floor for fixy-H-05.  The public `fixy::IsAccepted<Type, Grants...>`
// concept (Reject.h §IsAccepted) was inverted from FIXY-AUDIT-A8's design:
// the SIMPLE name now auto-injects `ImplicitTypeMarker` (a private
// `accept_default_strict_for<DimensionAxis::Type>` alias) into the
// underlying engagement check.  Callers that spell `strict<D::Type>`
// explicitly in the Grants pack thereby ENGAGE THE TYPE AXIS TWICE —
// once via auto-injection, once via the explicit grant — and
// `UniqueEngagementPerAxis` rejects the pack.
//
// Per CLAUDE.md §XVII.A7 + fixy-AUDIT-A7, callers MUST NOT spell
// `accept_default_strict_for<DimensionAxis::Type>` directly.  This
// fixture pins that the new wrapper-discipline `IsAccepted` enforces
// that rule structurally: 19 non-Type axes engaged + 1 explicit Type
// marker = REJECTED via the duplicate-axis path.
//
// The LOW-LEVEL form `fixy::IsAcceptedDirect<Type, Grants...>` remains
// available for code that genuinely needs to spell every engagement
// marker (e.g., Reject.h's own self-tests on `accepts_pack_v`).
//
// Distinct from neg_fixy_fn_static_assert_duplicate_axis.cpp (which
// targets fn<>'s class-body tier-4 static_assert chain via a duplicate
// Usage engagement): this fixture pins the CONCEPT-LEVEL gate at
// `fixy::IsAccepted` for a duplicate TYPE engagement caused by the
// wrapper-discipline auto-injection.
//
// Expected diagnostic: rejection at the `static_assert(fixy::IsAccepted<...>)`
// line below.  The full diagnostic chain cites
// `UniqueEngagementPerAxis` and the duplicate Type axis.

#include <crucible/fixy/Reject.h>

namespace fixy = crucible::fixy;
namespace gr   = crucible::fixy::grant;
using D        = crucible::fixy::dim::DimensionAxis;

template <D Axis>
using strict = gr::accept_default_strict_for<Axis>;

// 20-axis pack including explicit strict<D::Type> — duplicates the
// auto-injected ImplicitTypeMarker on the Type axis.
static_assert(fixy::IsAccepted<int,
    strict<D::Type>,                              // ← explicit Type marker
    strict<D::Refinement>, strict<D::Usage>,
    strict<D::Effect>, strict<D::Security>, strict<D::Protocol>,
    strict<D::Lifetime>, strict<D::Provenance>, strict<D::Trust>,
    strict<D::Representation>, strict<D::Observability>,
    strict<D::Complexity>, strict<D::Precision>, strict<D::Space>,
    strict<D::Overflow>, strict<D::Mutation>, strict<D::Reentrancy>,
    strict<D::Size>, strict<D::Version>, strict<D::Staleness>, strict<D::Synchronization>>,
    "fixy-H-05 floor: wrapper-discipline IsAccepted must reject "
    "explicit strict<D::Type> in the Grants pack (duplicate Type-axis "
    "engagement caused by ImplicitTypeMarker auto-injection).");

int main() { return 0; }
