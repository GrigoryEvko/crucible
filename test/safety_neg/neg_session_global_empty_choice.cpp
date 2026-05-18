// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A2-011 — substrate-side rejection of Choice<From, To> with
// ZERO branches at the global-wellformedness gate.
//
// Before the fix, `is_global_well_formed<Choice<From, To, Bs...>>`
// folded branch validity via `(... && ...)` over the empty `Bs` pack,
// which returns `true` vacuously.  Combined with `From ≠ To` admitting
// any two distinct roles, an empty-branch Choice silently passed the
// WF gate; the error eventually surfaced deep in projection
// (`plain_merge_t<>` is `is_same_v<>`-ill-formed at the leaf), but
// that's a confusing site — the diagnostic belongs at the highest-
// level gate.
//
// After the fix, an explicit `is_global_well_formed<Choice<From, To>,
// RecCtx>` specialization (zero-branch case) returns `false_type`,
// rejecting the structurally meaningless protocol upfront.
//
// Violation: top-level zero-branch Choice<Alice, Bob>.  The
// static_assert in this fixture expects WF to be true, so the assert
// fails.  Pairs with `neg_session_global_empty_choice_nested.cpp`
// (different rejection class: routed [Choice_Empty_Branches]
// diagnostic via assert_no_empty_choice helper, for empty Choice
// buried deeper in the type tree).
//
// Expected diagnostic: static_assert "should be well-formed" /
//                       static assertion failed / constraints not
//                       satisfied.

#include <crucible/sessions/SessionGlobal.h>

namespace neg_a2_011_empty_choice {
struct Alice {};
struct Bob   {};
}  // namespace neg_a2_011_empty_choice

namespace proto = ::crucible::safety::proto;

int main() {
    // Zero-branch Choice — structurally meaningless in MPST (no
    // selectable label can drive the protocol forward).
    using IllFormedG = proto::Choice<neg_a2_011_empty_choice::Alice,
                                     neg_a2_011_empty_choice::Bob>;

    // The static_assert fires:
    //
    //   "global type with Choice<F, T> and zero branches should be
    //    well-formed — but the fixy-A2-011 fix correctly rejects."
    //
    // Build failure here is the load-bearing outcome.
    static_assert(proto::is_global_well_formed_v<IllFormedG>,
                  "should be well-formed");
    return 0;
}
