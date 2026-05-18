// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A2-012 — `all_offers_have_crash_branch<VendorPinned<V, P>,
// PeerTag>` was the most LOAD-BEARING omission of the gap.  Every
// production CNTP upper-layer session — by design — pins a vendor
// via `VendorPinned<VendorBackend::*, InnerProto>` (the canonical
// shape per #1167 GAPS-068's per-vendor mint-substrate vendor-leq
// check).  Pre-fix, the SessionCrash.h primary forward-declared
// template at line 553 had no body, so EVERY production
// vendor-pinned crash-aware session triggered hard substitution
// failure at the static_assert — or worse, silently produced
// false-by-default in any SFINAE context, making BSYZ22
// crash-safety verification STRUCTURALLY VACUOUS for the entire
// CNTP transport layer.
//
// Violation: `VendorPinned<VendorBackend::NV, BadInner>` where
// BadInner is `Offer<Recv<Msg, End>>` lacking the
// Recv<Crash<UnreliablePeer>, _> branch.  Post-fix the walker
// recurses through the VendorPinned wrapper into BadInner and
// fires [CrashBranch_Missing_In_Tree] at the structural site.
//
// Pairs with `neg_session_crash_branch_vendorpinned_nested.cpp`
// (same trait, defect buried deeper in the vendor-pinned tree) —
// the pair proves transparent-recursion holds at both root and
// nested positions.
//
// Expected diagnostic: [CrashBranch_Missing_In_Tree] / static
//                       assertion failed.

#include <crucible/sessions/SessionCrash.h>

using namespace crucible::safety::proto;

namespace neg_a2_012_vp_root {
struct UnreliablePeer {};
struct Msg {};
}  // namespace neg_a2_012_vp_root

int main() {
    namespace ns = neg_a2_012_vp_root;

    // Inner protocol: Offer<> lacking the crash arm for
    // UnreliablePeer.  VendorPinned transparently wraps it; pre-
    // fix the wrapper hid the defect from the crash-walker.
    using BadInner = Offer<Recv<ns::Msg, End>>;
    using IllFormedProto = VendorPinned<VendorBackend::NV, BadInner>;

    assert_every_offer_has_crash_branch_for<IllFormedProto,
                                            ns::UnreliablePeer>();
    return 0;
}
