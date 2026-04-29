// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: calling AffinityMask::single() with a core index
// greater than kMaxCore (= 255 for the 256-bit mask shipped today).
//
// THE LOAD-BEARING UB FENCE.  Without the contract guard, shifting
// a uint64_t by an out-of-range bit position is C++ undefined
// behavior — the producer would silently emit garbage at the
// production NumaThreadPool worker-binding site.  The contract
// `pre(core <= kMaxCore)` makes the violation a compile-time
// error when the call is consteval-evaluated (this fixture's
// pattern), and a runtime contract-violation under the `enforce`
// semantic.
//
// This fixture is the FIRST contract-violation neg-fixture in
// the wrapper family — establishes the pattern for future
// per-axis contract fences (e.g., a future BitsBudget overflow
// guard, or an Epoch monotonicity check).
//
// [GCC-CONTRACTS-TEXT] — pre-condition rejection at consteval.

#include <crucible/algebra/lattices/AffinityLattice.h>

using namespace crucible::algebra::lattices;

// Force consteval evaluation by binding the result to a static
// constexpr — the contract pre-clause MUST fire because core=256
// exceeds kMaxCore=255.  GCC 16 reports the contract violation as
// a compile error referencing the failing pre-clause.
static constexpr AffinityMask bad =
    AffinityMask::single(static_cast<std::uint16_t>(AffinityMask::kMaxCore + 1));

int main() {
    return static_cast<int>(bad.popcount());
}
