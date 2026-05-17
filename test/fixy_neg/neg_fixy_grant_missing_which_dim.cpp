// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-AUDIT-A3 fixture: a Grant tag that satisfies IsGrantTag
// (final-class derived from grant_base) but has no `which_dim`
// specialization must reject at the resolver / Reject.h gate.
//
// Violation: `IsGrantTag` is purely structural (inherits grant_base
// + is_final).  An evil_grant satisfies IsGrantTag but lacks
// `which_dim<G>::value`, so `which_dim_v<G>` is ill-formed.
// Reject.h's AllDimsEngaged folds `which_dim_v<G>` over the pack;
// the undefined primary fires.
//
// Architectural intent: this is a designed-in rejection.  Without it,
// a third-party tag that happens to inherit grant_base (perhaps as
// part of a defensive mock) would silently project to nothing.
//
// Expected diagnostic: incomplete type / no member named 'value' /
// no matching specialization for which_dim<G>.

#include <crucible/fixy/Reject.h>

namespace fixy = crucible::fixy;

namespace neg_fixy_grant_missing_which_dim {

// Satisfies IsGrantTag (final + inherits grant_base) but has no
// which_dim specialization.
struct evil_grant final : ::crucible::fixy::grant::grant_base {};

}  // namespace neg_fixy_grant_missing_which_dim

int main() {
    namespace tags = neg_fixy_grant_missing_which_dim;

    // Should FAIL: the resolver tries which_dim_v<evil_grant> and
    // hits the undefined primary.
    static_assert(fixy::IsAcceptedGrants<tags::evil_grant>,
        "evil_grant must reject — no which_dim specialization.");
    return 0;
}
