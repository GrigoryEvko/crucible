// fixy_neg: §30.14 corpus rejects ghost × runtime-observable effect.
//
// HS14 floor for FIXY-AUDIT-G7 (Theory.h corpus expansion, Agent B).
// Theory.h's `ghost_runtime_observable` corpus entry detects the
// ghost-vs-runtime category error: a binding engaging both `ghost`
// (Usage=Ghost) AND any runtime-observable effect (Alloc / IO / Block
// / Bg) in `with<E...>`.
//
// Cite: Filliâtre-Gondelman-Paskevich 2014 "The Spirit of Ghost
// Code" (CAV / FMSD — the canonical formal statement of the
// ghost-vs-runtime discipline) / Leino 2010 "Dafny" (working
// verifier enforcing ghost-vs-concrete separation).  (fixy-CR-17
// replaced an earlier Müller-Schwerhoff-Summers 2016 "Viper"
// attribution: Viper is a verification IL that supports ghost as
// a syntactic feature but does not formalise the discipline; FGP
// 2014 is the canonical reference.)  Ghost values are erased at
// compile time and MUST NOT request runtime presence; allocating,
// performing I/O, blocking, or spawning background work from
// ghost code is structurally contradictory.
//
// Distinct from R006/P002 (CollisionCatalog marks_runtime_ghost_use):
// R006 requires an opt-in trait specialization at the use site.  This
// corpus entry catches the pattern at the binding's type-level effect
// row — no external trait needed.  Defense-in-depth: a binding
// rejected here never reaches the R006 use-site check.
//
// Reject sequence: IsAccepted → IsAcceptedDirect → NotInTheoryCorpus →  // fixy-A4-023: post-H-05 chain.
// `!ghost_runtime_observable::matches<>()` evaluates false → IsAccepted
// concept fails → mint_fn signature substitution fails.
//
// Expected diagnostic: "NotInTheoryCorpus" — the satisfaction-failure
// chain names the corpus gate.

#include <crucible/fixy/Fn.h>

namespace fixy = crucible::fixy;
namespace gr   = crucible::fixy::grant;
using D        = crucible::fixy::dim::DimensionAxis;

template <D Axis>
using strict = gr::accept_default_strict_for<Axis>;

int main() {
    // 19-axis pack engaging:
    //   Usage  = ghost     (compile-time-erased binding)
    //   Effect = with_io   (requests runtime I/O)
    //   contradictory by construction.
    auto bad = fixy::mint_fn<int,
        strict<D::Refinement>,
        gr::ghost,                  // Usage = Ghost
        gr::with_io,                // Effect = IO (observable)
        strict<D::Security>, strict<D::Protocol>, strict<D::Lifetime>,
        strict<D::Provenance>, strict<D::Trust>, strict<D::Representation>,
        strict<D::Observability>, strict<D::Complexity>, strict<D::Precision>,
        strict<D::Space>, strict<D::Overflow>, strict<D::Mutation>,
        strict<D::Reentrancy>, strict<D::Size>, strict<D::Version>,
        strict<D::Staleness>, strict<D::Synchronization>, strict<D::Regime>,
        strict<D::FpMode>>(42);
    (void)bad;
    return 0;
}
