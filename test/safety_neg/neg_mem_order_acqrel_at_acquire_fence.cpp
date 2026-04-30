// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: passing a `MemOrder<AcqRel, T>` value to a function
// whose `requires` clause demands `MemOrder::satisfies<Acquire>`.
// Companion to the main FOUND-G32 fixture (SeqCst at Acquire fence)
// — proves the rejection is structural across the lattice, NOT a
// SeqCst-special-case.
//
// AcqRel is one tier weaker than Acquire in the hardware-friendliness
// lattice: AcqRel ⊑ Release ⊑ Acquire ⊑ Relaxed.  satisfies<Acquire>
// = leq(Acquire, AcqRel) = false.  The requires-clause rejects.
//
// Concrete bug-class: a refactor adds a write that needs Release on
// the same atomic that previously had pure Acquire-load semantics,
// "upgrading" the operation to AcqRel for the RMW form.  This is
// SEMANTICALLY MORE EXPENSIVE (full both-direction barrier) than the
// previous Acquire-only load.  Hot-path readers that only needed
// Acquire-tier guarantees pay for the unnecessary store-side ordering
// — death by a thousand cuts on a high-fanout broadcast (Augur metrics
// snapshot pattern).  The wrapper catches the upgrade at the
// consumer-fence boundary.
//
// [GCC-WRAPPER-TEXT] — requires-clause rejection of cross-tier flow.

#include <crucible/safety/MemOrder.h>

#include <utility>

using namespace crucible::safety;

template <typename W>
    requires (W::template satisfies<MemOrderTag_v::Acquire>)
static int acquire_fence_consumer(W wrapped) noexcept {
    return std::move(wrapped).consume();
}

int main() {
    // Pinned at AcqRel — origin emits a full both-direction barrier
    // (memory_order_acq_rel on a fetch_add or similar RMW).  More
    // expensive than the Acquire-load that hot-path consumers depend on.
    MemOrder<MemOrderTag_v::AcqRel, int> acqrel_value{42};

    // Should FAIL: AcqRel does NOT satisfy Acquire (one tier weaker
    // in hardware-friendliness).  Captures the "upgrade-to-AcqRel
    // crept onto the hot path" regression class.
    int result = acquire_fence_consumer(std::move(acqrel_value));
    return result;
}
