// ── test_fixy_cheat_probe — Round 1 trait-spec-injection corpus ────
//
// Five adversarial cheats against fixy::IsAccepted's structural
// concept gate.  Each cheat MUST be rejected by the concept; if the
// build accepts any of these, the discipline is bypassed.
//
// Per misc/16_05_2026_fixy.md §4 Phase A: "1 cheat-probe round: 5
// attempts to bypass IsAccepted (e.g., trait-spec injection on
// Engaged, default-template specialization, friend-class
// circumvention)".
//
// All five claims are negative — `static_assert(!cheat::passes)`.
// A green compile means: the cheats all failed to escape the gate.

#include <crucible/fixy/Reject.h>

namespace fixy = crucible::fixy;
namespace gr   = crucible::fixy::grant;
using D        = crucible::fixy::dim::DimensionAxis;

template <D Axis>
using strict = gr::accept_default_strict_for<Axis>;

// ─── Cheat 1: User-defined empty struct posing as a grant ──────────
//
// The attacker writes a final empty type without inheriting
// grant_base, hoping IsGrantTag's structural check would accept it
// because it looks "grant-shaped" (empty, final, etc.).
//
// Defense: IsGrantTag requires `is_base_of_v<grant_base, T>`.  The
// rogue type doesn't inherit grant_base → IsGrantTag is false → the
// rogue type can't engage any axis → IsAcceptedGrants rejects.

namespace cheat_1_user_empty_type {
    struct rogue final {};  // posing as a grant — no grant_base inheritance
    static_assert(!fixy::grant::IsGrantTag<rogue>,
        "Cheat 1: a final empty user type without grant_base must NOT "
        "satisfy IsGrantTag.");

    // Even if we add it to a pack, it engages no axis — IsAccepted
    // still rejects because every dim is now unengaged.
    static_assert(!fixy::IsAccepted<int, rogue>,
        "Cheat 1: a rogue type cannot satisfy the engagement check.");
}

// ─── Cheat 2: Subclass an existing grant tag to inject behavior ────
//
// The attacker subclasses a known grant (e.g., grant::copy) to
// inherit grant_base "for free", then specializes some downstream
// trait against the subclass.
//
// Defense: every grant tag is `final` (per safety/NotInherited.h).
// The compiler rejects the subclass declaration entirely; if it
// somehow slipped through (e.g., via private inheritance), the
// IsGrantTag concept rechecks `is_final_v<T>` on the type queried.

namespace cheat_2_subclass_injection {
    // Direct subclass — would fail to compile because grant::copy is
    // declared `final`.  Per the discipline, we exercise the concept
    // gate on a different rogue: a struct that LOOKS like it
    // inherits from grant_base but is itself non-final.
    struct rogue_nonfinal : fixy::grant::grant_base {};  // non-final
    static_assert(!fixy::grant::IsGrantTag<rogue_nonfinal>,
        "Cheat 2: a non-final type inheriting grant_base must NOT "
        "satisfy IsGrantTag — the final-class check defends against "
        "subclass-injection of behavior.");
}

// ─── Cheat 3: Specialize which_dim for a foreign type ──────────────
//
// The attacker tries to inject by specializing `which_dim<X>` for a
// non-grant type X, hoping IsAcceptedGrants will accept X because
// the dim mapping exists.
//
// Defense: IsAcceptedGrants gates on `AllGrantsWellFormed` BEFORE
// reading which_dim.  A type that doesn't satisfy IsGrantTag is
// rejected regardless of which_dim's behavior.

namespace cheat_3_foreign_which_dim {
    struct foreign {};  // not a grant — no grant_base, no final
}
// Inject a which_dim specialization for the foreign type:
// fixy-CR-09: known residual gap — this cheat probe intentionally
// reopens `namespace crucible::fixy::grant` to demonstrate that C++
// has no namespace-scoped specialization access control.  The
// `IsGrantTag` gate defends regardless of which_dim's behavior; this
// reopen is the proof that the gate is the load-bearing defense, not
// the namespace.  The namespace-purity CI guard
// (scripts/check-fixy-grant-namespace-purity.sh) excepts this file
// because the attack-demonstration intent is documented above.
namespace crucible::fixy::grant {
    template <>
    struct which_dim<::cheat_3_foreign_which_dim::foreign>
        : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Usage> {};
}
namespace cheat_3_foreign_which_dim {
    static_assert(!fixy::IsAccepted<int, foreign>,
        "Cheat 3: specializing which_dim<> for a non-grant type must "
        "NOT bypass IsAcceptedGrants — the IsGrantTag gate fires "
        "before which_dim is consulted.");
}

// ─── Cheat 4: Empty pack with non-default Type ─────────────────────
//
// The attacker passes a sensible Type but no grants at all, hoping
// the auto-injected Type marker (per fixy-H-05's wrapper-discipline
// IsAccepted) would carry the rest of the pack.
//
// Defense: IsAccepted requires EVERY dim engaged.  The auto-injected
// ImplicitTypeMarker engages only Type; the remaining 18 axes stay
// unengaged.  AllDimsEngaged fails the whole concept.

namespace cheat_4_empty_pack_with_type {
    static_assert(!fixy::IsAccepted<int>,
        "Cheat 4: the auto-injected Type marker alone does not "
        "satisfy IsAccepted — the other 18 axes are still unengaged.");
}

// ─── Cheat 5: Half-engagement — 9 strict + 10 omitted ──────────────
//
// The attacker engages roughly half the axes (Type via auto-injection
// + 9 explicit non-Type axes), hoping IsAcceptedGrants computes
// "engaged" via majority rule or short-circuits on the first 10.
//
// Defense: AllDimsEngaged is a conjunction (logical AND) over every
// dim.  Any unengaged axis fails the whole concept.

namespace cheat_5_half_engagement {
    static_assert(!fixy::IsAccepted<int,
        strict<D::Refinement>, strict<D::Usage>,
        strict<D::Effect>, strict<D::Security>, strict<D::Protocol>,
        strict<D::Lifetime>, strict<D::Provenance>, strict<D::Trust>,
        strict<D::Representation>>,
        "Cheat 5: 10-of-20 engagement (Type via injection + 9 "
        "explicit) must reject — the other 10 dims are still "
        "unengaged.");
}

// ─── Sanity counter-witness — the actual full pack accepts ─────────
//
// To ensure the cheat probe isn't trivially passing because
// IsAccepted is broken-shut.  Under wrapper-discipline IsAccepted,
// the witness omits strict<D::Type> (auto-injected) and engages the
// 19 non-Type axes explicitly.

namespace counter_witness_accepts {
    static_assert(fixy::IsAccepted<int,
        strict<D::Refinement>, strict<D::Usage>,
        strict<D::Effect>, strict<D::Security>, strict<D::Protocol>,
        strict<D::Lifetime>, strict<D::Provenance>, strict<D::Trust>,
        strict<D::Representation>, strict<D::Observability>,
        strict<D::Complexity>, strict<D::Precision>, strict<D::Space>,
        strict<D::Overflow>, strict<D::Mutation>, strict<D::Reentrancy>,
        strict<D::Size>, strict<D::Version>, strict<D::Staleness>, strict<D::Synchronization>, strict<D::Regime>,
        strict<D::FpMode>>,
        "Counter-witness: a fully-engaged strict pack MUST accept "
        "(IsAccepted is not broken-shut).");
}

int main() { return 0; }
