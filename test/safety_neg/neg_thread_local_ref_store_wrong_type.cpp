// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: calling ThreadLocalRef<Tag, T>::store(U&&) with a U
// that is NOT assignable to T&.  The store's requires-clause
// `std::is_assignable_v<T&, U&&>` MUST reject the call.
//
// Concrete bug-class this catches: a refactor that loosened
// ThreadLocalRef::store's requires-clause — e.g. dropped it or
// replaced with `true` — would silently accept a wrong-type
// argument, then fail inside the assignment with an opaque
// `error: assignment of <U> to <T> not viable` diagnostic OR,
// worse, succeed via an implicit conversion the user did not
// intend (e.g. const char* → bool decay).  This fixture pins
// the rejection AT the store boundary, matching the §XXI promise
// that wrong-type stores fail at the assignability gate, not
// inside the substrate's operator=.
//
// HS14 #2 of 2 for V-080 — pairs with neg_thread_local_ref_mint_
// requires_default_ctor for the 2-fixture floor across distinct
// mismatch classes:
//   1. mint-requires-default-ctor: substrate constructibility gate.
//   2. store-wrong-type (this):    substrate assignability gate.
//
// [GCC-WRAPPER-TEXT] — requires-clause rejection on
// ThreadLocalRef::store.

#include <crucible/safety/ThreadLocalRef.h>

#include <string>

using namespace crucible::safety;

struct CounterTag {};

// Wrapper around `int` with NO converting assignment from std::string
// — the assignability gate must reject the store.
struct PlainCounter {
    int value{0};
    constexpr PlainCounter() noexcept = default;
    // No `operator=(std::string const&)` declared — `is_assignable_v<
    // PlainCounter&, std::string const&>` is FALSE.
};

int main() {
    ThreadLocalRef<CounterTag, PlainCounter> ref{};

    // Should FAIL: store<std::string>(...) on a PlainCounter cell.
    // The requires-clause `std::is_assignable_v<PlainCounter&,
    // std::string const&>` is FALSE — no converting assignment exists.
    std::string s{"not_a_counter"};
    ref.store(s);
    return ref.peek().value;
}
