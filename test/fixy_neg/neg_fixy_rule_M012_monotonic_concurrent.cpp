// NEGATIVE-COMPILE TEST.  MUST FAIL TO COMPILE.
//
// FIXY-B4 — proves the substrate's §6.8 ValidComposition gate fires
// THROUGH fixy::fn<...> when grant composition triggers a collision
// rule.  M012 forbids `mutation_v == Monotonic && concurrent_context
// && repr_v != Atomic` — the canonical "monotonic counter shared
// across threads without an atomic carrier" bug class.
//
// Trigger via fixy::fn:
//   - grant::monotonic_advance → Mutation::Monotonic
//   - grant::with<Bg>          → effect row contains Bg
//                                 (substrate's concurrent_context_v
//                                 fires on row_has_effect_v<R, Bg>)
//   - Representation defaults  → Opaque (≠ Atomic)
//
// Substrate emits the M012_MonotonicConcurrentNoAtomic diagnostic.
//
// Why ONLY M012 in Phase B: the other 11 §6.8 rules (L002 borrow×
// async, E044 CT×async, etc.) require substrate-side opt-in marker
// traits (marks_async / marks_ct / marks_fail / marks_runtime_ghost_use
// / is_exact_decimal / marks_concurrent_context).  Phase A's
// Grant.h tags don't propagate those markers — Phase C threads them
// through `protocol_session<P>` + `complexity_*` + dedicated CT
// marker grants.  Until then, only M012 (which derives concurrent
// context purely from EffectRow containing Bg) is reachable via the
// fixy path.

#include <crucible/fixy/Fn.h>

namespace cf = crucible::fixy;
namespace cd = crucible::fixy::dim;
namespace cg = crucible::fixy::grant;
namespace ce = crucible::effects;

using ViolatingFn = cf::fn<int,
    cg::monotonic_advance,                          // Mutation = Monotonic
    cg::with<ce::Effect::Bg>,                       // EffectRow ⊇ {Bg} → concurrent context
    cf::accept_default_strict_for<cd::Type>,
    cf::accept_default_strict_for<cd::Refinement>,
    cf::accept_default_strict_for<cd::Usage>,
    cf::accept_default_strict_for<cd::Security>,
    cf::accept_default_strict_for<cd::Protocol>,
    cf::accept_default_strict_for<cd::Lifetime>,
    cf::accept_default_strict_for<cd::Provenance>,
    cf::accept_default_strict_for<cd::Trust>,
    cf::accept_default_strict_for<cd::Representation>,  // = Opaque, NOT Atomic
    cf::accept_default_strict_for<cd::Observability>,
    cf::accept_default_strict_for<cd::Complexity>,
    cf::accept_default_strict_for<cd::Precision>,
    cf::accept_default_strict_for<cd::Space>,
    cf::accept_default_strict_for<cd::Overflow>,
    cf::accept_default_strict_for<cd::Reentrancy>,
    cf::accept_default_strict_for<cd::Size>,
    cf::accept_default_strict_for<cd::Version>,
    cf::accept_default_strict_for<cd::Staleness>>;

// IsAccepted passes (every dim engaged).  ValidComposition rejects
// at the underlying safety::fn::Fn<...> instantiation with the M012
// diagnostic.
static_assert(sizeof(ViolatingFn) > 0, "M012");  // forces resolution

int main() { return 0; }
