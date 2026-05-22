// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: calling mint_thread_local_ref<Tag, T>() when T is NOT
// default-constructible.  The requires-clause
// `std::is_default_constructible_v<T>` MUST reject the call.
//
// Concrete bug-class this catches: a refactor that loosened
// mint_thread_local_ref's requires-clause — e.g. dropped it or
// replaced with `true` — would silently accept a non-default-
// constructible T.  Then the first per-thread access of the cell
// would attempt `thread_local T storage{}` value-initialization,
// failing inside the static helper with an opaque diagnostic
// deep below the mint boundary.  This fixture pins the rejection
// AT the mint, matching the §XXI promise that
// `grep "mint_thread_local_ref"` finds every site where the
// constructibility constraint fires.
//
// Pairs with neg_thread_local_ref_store_wrong_type for the 2-
// fixture HS14 floor across distinct mismatch classes:
//   1. mint-requires-default-ctor (this): substrate constructibility
//      gate at mint/class level.
//   2. store-wrong-type:                  substrate assignability
//      gate at store boundary.
//
// [GCC-WRAPPER-TEXT] — requires-clause rejection on
// mint_thread_local_ref / ThreadLocalRef class template.

#include <crucible/safety/ThreadLocalRef.h>

using namespace crucible::safety;

// A class T with NO default constructor — only an int-taking ctor.
// The class-level + mint-level requires-clause
// `std::is_default_constructible_v<T>` must reject every instantiation.
struct NoDefaultCtor {
    int value;
    constexpr explicit NoDefaultCtor(int v) noexcept : value{v} {}
    NoDefaultCtor() = delete;
};

struct CounterTag {};

int main() {
    // Should FAIL: mint_thread_local_ref<CounterTag, NoDefaultCtor>()
    // — the requires-clause `std::is_default_constructible_v<
    // NoDefaultCtor>` is FALSE, so the mint overload is excluded
    // AND the class template's own requires-clause rejects the
    // implicit instantiation.
    auto bad = mint_thread_local_ref<CounterTag, NoDefaultCtor>();
    return bad.peek().value;
}
