// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A2-011 — substrate-side routed diagnostic for empty Choice
// NESTED inside a well-formed prefix.  Distinct rejection class from
// `neg_session_global_empty_choice.cpp` (top-level WF rejection):
// this fixture exercises the recursive `has_empty_choice_v<G>` trait
// + the `assert_no_empty_choice<G>()` consteval helper which fires
// the framework-controlled [Choice_Empty_Branches] tag prefix
// (stable across GCC versions, analogous to the
// [ProtocolViolation_Self_Loop] discipline #371).
//
// Violation: outer Rec_G whose body is a Transmission whose
// continuation is a Choice<Bob, Carol> with zero branches.  The
// outer prefix is fine; the inner empty Choice contaminates the
// tree.  The has_empty_choice_v walk recurses through Rec_G's body,
// Transmission's continuation, and Choice's branches' continuations,
// surfacing the empty case at any depth.
//
// Why this fixture pairs with the top-level one: empty Choices can
// hide inside Rec_G bodies or branch continuations, so a single root-
// level WF rejection is insufficient — the recursive walk closes
// the nested-empty-Choice gap that fixy-CR-14 was a sibling of (per
// the task description).
//
// Expected diagnostic: [Choice_Empty_Branches] / static assertion
//                       failed / constraints not satisfied.

#include <crucible/sessions/SessionGlobal.h>

namespace neg_a2_011_empty_choice_nested {
struct Alice {};
struct Bob   {};
struct Carol {};
struct Ping  {};
}  // namespace neg_a2_011_empty_choice_nested

namespace proto = ::crucible::safety::proto;

int main() {
    namespace ns = neg_a2_011_empty_choice_nested;

    // Outer prefix is well-formed: Rec_G body sends Alice→Bob; the
    // inner Choice<Bob, Carol> with no branches is the violation
    // (buried two levels deep — Rec_G ▷ Transmission ▷ Choice).
    using IllFormedG =
        proto::Rec_G<
            proto::Transmission<ns::Alice, ns::Bob, ns::Ping,
                proto::Choice<ns::Bob, ns::Carol>>>;

    // Fires the framework-controlled [Choice_Empty_Branches] tag
    // prefix; the routed-diagnostic helper recurses into Rec_G body,
    // Transmission continuation, Choice branches' continuations,
    // and the Choice's variadic Bs pack itself.
    proto::assert_no_empty_choice<IllFormedG>();
    return 0;
}
