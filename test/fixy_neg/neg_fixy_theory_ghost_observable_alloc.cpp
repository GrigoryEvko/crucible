// fixy_neg: §30.14 corpus rejects ghost × Alloc effect (HS14 ≥2 floor).
//
// Second fixture for the `ghost_runtime_observable` corpus entry per
// fixy-M-14.  HS14 mandates ≥2 negative-compile fixtures per
// soundness-gating predicate, demonstrating the gate fires on each
// kind of mismatch.  The corpus rule catches "ghost × any
// runtime-observable effect"; the existing
// `neg_fixy_theory_ghost_observable.cpp` only proves the gate fires
// for IO.  This fixture proves it fires for Alloc — a structurally
// distinct mismatch class because Alloc requests heap presence at
// runtime, whereas IO requests external side effects.
//
// Cite: Filliâtre-Gondelman-Paskevich 2014 "The Spirit of Ghost
// Code" — the rule is "ghost code must not request runtime
// presence", and the runtime-presence axis is multi-valued (heap,
// I/O, blocking, bg work).  A single-effect fixture leaves the
// other three runtime-observable effects uncovered.
//
// Reject sequence: identical to the IO variant — IsAccepted →
// IsAcceptedDirect → NotInTheoryCorpus → matcher evaluates false
// because `with_alloc` carries `Effect::Alloc` which is in the
// runtime-observable set.

#include <crucible/fixy/Fn.h>

namespace fixy = crucible::fixy;
namespace gr   = crucible::fixy::grant;
using D        = crucible::fixy::dim::DimensionAxis;

template <D Axis>
using strict = gr::accept_default_strict_for<Axis>;

int main() {
    // 19-axis pack engaging:
    //   Usage  = ghost      (compile-time-erased binding)
    //   Effect = with_alloc (requests runtime heap presence)
    //   contradictory by construction — Alloc is runtime-observable.
    auto bad = fixy::mint_fn<int,
        strict<D::Refinement>,
        gr::ghost,                  // Usage = Ghost
        gr::with_alloc,             // Effect = Alloc (observable)
        strict<D::Security>, strict<D::Protocol>, strict<D::Lifetime>,
        strict<D::Provenance>, strict<D::Trust>, strict<D::Representation>,
        strict<D::Observability>, strict<D::Complexity>, strict<D::Precision>,
        strict<D::Space>, strict<D::Overflow>, strict<D::Mutation>,
        strict<D::Reentrancy>, strict<D::Size>, strict<D::Version>,
        strict<D::Staleness>, strict<D::Synchronization>, strict<D::Regime>,
        strict<D::FpMode>, strict<D::SyscallSurface>, strict<D::ControlFlow>, strict<D::CallShape>, strict<D::StackUse>, strict<D::GlobalState>, strict<D::Stdio>, strict<D::HwInstruction>, strict<D::BarrierStrength>, strict<D::SimdIsa>, strict<D::MemoryScope>>(42);
    (void)bad;
    return 0;
}
