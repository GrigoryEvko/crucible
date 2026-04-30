// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: passing a `MemOrder<SeqCst, T>` value to a function
// whose `requires` clause demands `MemOrder::satisfies<Relaxed>` —
// the THREE-TIER-GAP rejection.  Strongest possible rejection
// claim in the lattice: SeqCst is at the bottom (weakest hardware-
// friendliness), Relaxed at the top (strongest).
//
// SeqCst::satisfies<Relaxed> = leq(Relaxed, SeqCst) = false.
// SeqCst is THREE positions below Relaxed in the chain
// (SeqCst ⊑ AcqRel ⊑ Release ⊑ Acquire ⊑ Relaxed); a Relaxed-fence
// consumer (foreground hot-path inner-loop counter increment) is
// the WORST possible target for a SeqCst-emitting site.
//
// Concrete bug-class: a counter declared at a Relaxed-friendly site
// (e.g., per-thread bump counter that doesn't synchronize with any
// reader) is "fixed" by a refactor swapping in atomic<>.fetch_add
// with default memory_order (= seq_cst).  Foreground per-op cost
// jumps from ~1 ns (Relaxed) to ~30 ns (SeqCst with mfence on x86).
// On a 1-Mop/s loop that's a 30 ms regression per second; on hot-
// path recording, fatal.
//
// [GCC-WRAPPER-TEXT] — requires-clause rejection of cross-tier flow.

#include <crucible/safety/MemOrder.h>

#include <utility>

using namespace crucible::safety;

template <typename W>
    requires (W::template satisfies<MemOrderTag_v::Relaxed>)
static int relaxed_fence_consumer(W wrapped) noexcept {
    return std::move(wrapped).consume();
}

int main() {
    MemOrder<MemOrderTag_v::SeqCst, int> seqcst_value{1};

    // Should FAIL: SeqCst is the weakest hardware-friendliness claim;
    // Relaxed-tier consumers (the strictest fence requirement) reject
    // it — SeqCst emits ~30× more memory-ordering machinery than the
    // Relaxed contract permits.  THREE-tier gap = strongest rejection.
    int result = relaxed_fence_consumer(std::move(seqcst_value));
    return result;
}
