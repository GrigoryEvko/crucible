// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #2 of 2 for safety::CyclicBuffer<T, N> (#1084 - CyclicBuffer
// piece).
//
// Premise: CyclicBuffer<T, N>::push is gated on assignability —
// `push(T const&) requires std::is_copy_assignable_v<T>` and
// `push(T&&) requires std::is_move_assignable_v<T>`.  A T that is
// default-constructible (so the FixedArray<T,N> slots still build) but
// NOT assignable MUST make BOTH push overloads disappear from the
// candidate set, leaving claim() as the only way to populate a slot.
//
// This is the MEMBER-FUNCTION rejection (distinct from fixture #1's
// CLASS-TEMPLATE rejection): the class instantiates fine — claim() is
// available — but push() is removed because its trailing requires-clause
// is unsatisfied.  Without the gate, push would hard-error deep inside
// `slot = value` instead of cleanly removing itself; the requires-clause
// turns a confusing internal error into "no matching function for push".
//
// Expected diagnostic: "no matching function for call to ... push" /
// "constraints not satisfied" / "associated constraints are not
// satisfied" pointing at the push call.

#include <crucible/safety/CyclicBuffer.h>

namespace saf = crucible::safety;

// Default-constructible + copyable-by-construction, but NOT assignable:
// the FixedArray<NonAssignable, 8> slots default-init fine, so the class
// instantiates; only push's assignability requires-clause fails.
struct NonAssignable {
    int value = 0;
    NonAssignable()                                = default;
    NonAssignable(NonAssignable const&)            = default;
    NonAssignable(NonAssignable&&)                 = default;
    NonAssignable& operator=(NonAssignable const&) = delete;
    NonAssignable& operator=(NonAssignable&&)      = delete;
    ~NonAssignable()                               = default;
};

int main() {
    saf::CyclicBuffer<NonAssignable, 8> buffer{};

    // claim() is fine — returns a reference, no assignment needed.
    NonAssignable& slot = buffer.claim();
    (void)slot;

    // Bridge fires: NonAssignable is neither copy- nor move-assignable →
    // both push overloads' requires-clauses are unsatisfied → no matching
    // function for push.
    buffer.push(NonAssignable{});
    return 0;
}
