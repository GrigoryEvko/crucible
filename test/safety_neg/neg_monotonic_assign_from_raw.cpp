// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// PROD-WRAP-8 (#537): Graph::num_nodes_ field type is now
// safety::Monotonic<uint32_t> instead of bare uint32_t.  The discipline
// this enforces — "the counter only ever advances, never resets or
// rewinds" — relies on Monotonic NOT being assignable from a raw T.
// If a future refactor re-added an implicit `operator=(uint32_t)` (or
// a converting assignment), production code could silently bypass the
// monotonicity contract by writing `num_nodes_ = some_value` and the
// invariant would dissolve.
//
// This fixture pins the absence: assigning a raw uint32_t to a
// Monotonic<uint32_t> must fail to compile with a "no viable
// overload" / "cannot convert" diagnostic.  The legitimate mutation
// path is `bump()` (advance by 1, contract-checked) or `advance(v)`
// (advance to v, contract-checked v >= current).
//
// Expected diagnostic: GCC 16 reports the no-matching-assignment-
// operator failure.

#include <crucible/safety/Mutation.h>

#include <cstdint>

using crucible::safety::Monotonic;

int main() {
    Monotonic<uint32_t> counter{0};
    counter = 42u;   // ← MUST fail: Monotonic has no assignment from T
    return 0;
}
