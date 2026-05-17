// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-AUDIT-D8 fixture #2: mint_session_handle<Proto, Resource>
// rejects a non-SessionResource via its third static_assert, keyed
// on the `SessionResource<Resource>` concept.
//
// Substrate diagnostic tag: "[SessionResource_NotPinned]" — Resource
// must be either a value type (passed by value) or an lvalue
// reference to a type derived from `safety::Pinned<T>`.  An lvalue
// reference to a NON-Pinned-derived type fails the concept.
//
// Violation: explicitly specialize Resource to `int&` (an lvalue
// reference to a non-Pinned type).  `int` does not derive from
// `Pinned<int>`, so SessionResource<int&> is false and the
// static_assert fires.  Explicit Resource specialization is
// required because pass-by-value template deduction would otherwise
// drop the reference qualifier and admit bare `int` as a value
// type.
//
// Expected diagnostic: GCC emits the static_assert message with the
// "[SessionResource_NotPinned]" tag.

#include <crucible/sessions/Session.h>

namespace cp = crucible::safety::proto;

int main() {
    // Send<int, End> IS a well-formed protocol — so the FIRST two
    // static_asserts pass, isolating the resource-check failure.
    using OkProto = cp::Send<int, cp::End>;

    int not_a_resource = 0;

    // Explicitly specialize Resource = int& — an lvalue reference to
    // a non-Pinned type.  SessionResource<int&> is false (int does
    // not derive from Pinned<int>); the static_assert fires.
    (void)cp::mint_session_handle<OkProto, int&>(not_a_resource);
    return 0;
}
