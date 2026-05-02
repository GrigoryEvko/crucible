// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// PROD-WRAP-8 (#537): Graph::num_nodes_ field type is now
// safety::Monotonic<uint32_t> instead of bare uint32_t.  Reading the
// counter requires the explicit `.get()` accessor; there is NO
// implicit conversion to uint32_t.
//
// Why this matters: the call-site sweep that landed with #537 had
// to rewrite ~30 read sites from `num_nodes_` to `num_nodes_.get()`.
// If a future refactor added an implicit `operator uint32_t() const`
// to Monotonic, the explicit-call discipline would erode silently —
// future field migrations from raw uint32_t to Monotonic<uint32_t>
// could land without touching any read site, hiding the type-system
// upgrade and defeating grep-discoverability of "where do we read
// the counter?".
//
// This fixture pins the absence: assigning a Monotonic<uint32_t> to
// a uint32_t variable must fail to compile with a "cannot convert" /
// "no viable conversion" diagnostic.  The legitimate read path is
// `counter.get()` (returns const T&).
//
// Expected diagnostic: GCC 16 reports the implicit-conversion failure.

#include <crucible/safety/Mutation.h>

#include <cstdint>

using crucible::safety::Monotonic;

int main() {
    Monotonic<uint32_t> counter{0};
    uint32_t snapshot = counter;   // ← MUST fail: no implicit conversion
    (void)snapshot;
    return 0;
}
