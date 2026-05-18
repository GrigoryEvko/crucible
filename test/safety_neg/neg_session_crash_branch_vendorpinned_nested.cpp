// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A2-012 — companion to
// `neg_session_crash_branch_vendorpinned_missing.cpp`.  Here the
// missing-crash-arm Offer<> is buried TWO LEVELS deep:
//
//     VendorPinned<NV,
//         Send<Setup,
//             Recv<Ack,
//                 BadOfferLackingCrashArm>>>
//
// The outer prefix is structurally crash-safe (no Offer<> on the
// way down).  The wrapped inner protocol's Send/Recv chain leads
// to an Offer that lacks Recv<Crash<UnreliablePeer>, _>.  Pre-fix
// the walker couldn't enter the VendorPinned wrapper at all
// (substitution failure), so the nested defect was completely
// invisible.  Post-fix the walker recurses through VendorPinned
// → Send → Recv → Offer and fires [CrashBranch_Missing_In_Tree]
// at the nested site — proving the recursion is transparent to
// vendor-pin wrappers at any depth (not just root-level).
//
// Why both fixtures: VendorPinned typically wraps the WHOLE
// production protocol at root level (the canonical CNTP shape).
// Nested VendorPinned is less common but legal — both must walk.
// The root-level fixture pins the trivial case; this one pins the
// recursive case.  Together they close the structural gap.
//
// Expected diagnostic: [CrashBranch_Missing_In_Tree] / static
//                       assertion failed.

#include <crucible/sessions/SessionCrash.h>

using namespace crucible::safety::proto;

namespace neg_a2_012_vp_nested {
struct UnreliablePeer {};
struct Setup {};
struct Ack   {};
struct Payload {};
}  // namespace neg_a2_012_vp_nested

int main() {
    namespace ns = neg_a2_012_vp_nested;

    // Deeply-buried Offer<> lacking the crash arm.
    using BadOffer = Offer<Recv<ns::Payload, End>>;

    // Outer prefix is well-formed: Send<Setup, Recv<Ack, BadOffer>>.
    // Wrapped under VendorPinned<NV, ...> at root.
    using NestedBadProto =
        Send<ns::Setup,
             Recv<ns::Ack, BadOffer>>;
    using IllFormedProto = VendorPinned<VendorBackend::AMD, NestedBadProto>;

    assert_every_offer_has_crash_branch_for<IllFormedProto,
                                            ns::UnreliablePeer>();
    return 0;
}
