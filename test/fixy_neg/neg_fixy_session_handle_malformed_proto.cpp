// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-AUDIT-D8 fixture #1: mint_session_handle<Proto>(resource)
// rejects an ill-formed protocol via its first static_assert,
// keyed on `is_well_formed_v<Proto>`.
//
// Substrate diagnostic tag: "[Protocol_Ill_Formed]" with the cause
// "Most likely cause: a Continue appears outside any enclosing Loop".
//
// Violation: pass `Continue` directly as the protocol.  Continue is
// the loop-jump marker that must appear inside a Loop<Body> shell;
// when supplied as a standalone protocol head, is_well_formed_v
// returns false and the first static_assert fires.
//
// Expected diagnostic: GCC emits the static_assert message with the
// "[Protocol_Ill_Formed]" tag.

#include <crucible/sessions/Session.h>

namespace cp = crucible::safety::proto;

// A value-type dummy resource that satisfies SessionResource (any
// non-reference type qualifies; the Pinned-or-value-type check is
// the resource gate).  The resource itself is legitimate; the
// rejection happens at the protocol-well-formedness check, not at
// the resource check.
struct NegSessionHandleMalformed_Channel {};

int main() {
    // Continue cannot stand alone — it must appear inside a Loop<Body>.
    // The static_assert(is_well_formed_v<Proto>) inside
    // mint_session_handle fires immediately.
    (void)cp::mint_session_handle<cp::Continue>(
        NegSessionHandleMalformed_Channel{});
    return 0;
}
