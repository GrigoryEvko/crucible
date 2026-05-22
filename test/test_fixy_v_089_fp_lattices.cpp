// FIXY-V-089 sentinel TU: 11 FP-mode ChainLattice algebras.
//
// V-089 ships:
//   1. 11 per-sub-axis Fp*Lattice structs (FpRoundingLattice,
//      FpFtzLattice, FpContractLattice, FpTrapMaskLattice,
//      FpDenormalInputLattice, FpNanPolicyLattice, FpInfPolicyLattice,
//      FpComplexLayoutLattice, FpLibmPolicyLattice, FpReassociateLattice,
//      FpConstantRoundingLattice) — each `ChainLatticeOps<E>`-derived
//      with explicit bottom/top + name() + At<T> singleton sub-lattice.
//   2. Per-axis `fp_<axis>_name(E)` consteval display function with a
//      reflection-driven name-coverage check (every enumerator has a
//      switch arm).
//   3. AllLattices.h umbrella registration — all 11 lattices added to
//      the `every_lattice_has_name<...>` pack so future contributors
//      cannot drop the `name()` member without lighting the diagnostic.
//   4. `fp_mode_lattice_runtime_smoke_test()` — runtime-arg invocation
//      of leq/join/meet for each sub-axis (per
//      feedback_algebra_runtime_smoke_test_discipline memory).
//
// V-089 ships NO Graded wrappers (V-090 ships the FpModeProductLattice
// composite + 11 safety/Fp*.h Graded<Modality, Lattice, T> carriers)
// and NO grant tags (V-092 ships fixy/Fp.h with the 12 grant tags).
//
// This sentinel TU witnesses that the 11 lattices are:
//   (a) discoverable via the umbrella include AllLattices.h,
//   (b) satisfy Lattice / BoundedLattice / !Semiring concept gates,
//   (c) algebraically well-formed (leq reflexive, join/meet idempotent,
//       absorbing-element identity laws hold for bottom/top),
//   (d) cross-lattice-distinct in the type system,
//   (e) exercisable at RUNTIME (smoke test invocation in main()).

#include <crucible/algebra/lattices/AllLattices.h>
#include <crucible/algebra/lattices/FpModeLattice.h>
#include <crucible/algebra/Lattice.h>
#include <crucible/algebra/Modality.h>

#include <string_view>
#include <type_traits>

namespace cal = ::crucible::algebra::lattices;
namespace ca  = ::crucible::algebra;

namespace {

// ── (a) Umbrella discovery — all 11 lattices reachable via include ──
//
// AllLattices.h's `every_lattice_has_name<...>` pack is the load-
// bearing reflection-coverage gate (fixy-A3-017).  A lattice header
// included WITHOUT being added to the pack ships a silent "unnamed
// lattice" diagnostic regression.  V-089 added all 11 to the pack;
// this static_assert pins that they remain reachable through the
// canonical umbrella entry point — if a future PR drops the include
// from AllLattices.h, this TU stops compiling.
static_assert(::crucible::algebra::HasLatticeName<cal::FpRoundingLattice>);
static_assert(::crucible::algebra::HasLatticeName<cal::FpFtzLattice>);
static_assert(::crucible::algebra::HasLatticeName<cal::FpContractLattice>);
static_assert(::crucible::algebra::HasLatticeName<cal::FpTrapMaskLattice>);
static_assert(::crucible::algebra::HasLatticeName<cal::FpDenormalInputLattice>);
static_assert(::crucible::algebra::HasLatticeName<cal::FpNanPolicyLattice>);
static_assert(::crucible::algebra::HasLatticeName<cal::FpInfPolicyLattice>);
static_assert(::crucible::algebra::HasLatticeName<cal::FpComplexLayoutLattice>);
static_assert(::crucible::algebra::HasLatticeName<cal::FpLibmPolicyLattice>);
static_assert(::crucible::algebra::HasLatticeName<cal::FpReassociateLattice>);
static_assert(::crucible::algebra::HasLatticeName<cal::FpConstantRoundingLattice>);

// ── (b) Concept gate sanity — every lattice satisfies BoundedLattice ─
//
// Defense-in-depth.  The per-lattice self-test blocks inside
// FpModeLattice.h already pin Lattice + BoundedLattice + !Semiring via
// the CRUCIBLE_FP_LATTICE_VERIFY macro; pinning the gates here at the
// sentinel-TU level guards against a future header refactor that
// accidentally drops the macro invocation.
static_assert(ca::BoundedLattice<cal::FpRoundingLattice>);
static_assert(ca::BoundedLattice<cal::FpFtzLattice>);
static_assert(ca::BoundedLattice<cal::FpContractLattice>);
static_assert(ca::BoundedLattice<cal::FpTrapMaskLattice>);
static_assert(ca::BoundedLattice<cal::FpDenormalInputLattice>);
static_assert(ca::BoundedLattice<cal::FpNanPolicyLattice>);
static_assert(ca::BoundedLattice<cal::FpInfPolicyLattice>);
static_assert(ca::BoundedLattice<cal::FpComplexLayoutLattice>);
static_assert(ca::BoundedLattice<cal::FpLibmPolicyLattice>);
static_assert(ca::BoundedLattice<cal::FpReassociateLattice>);
static_assert(ca::BoundedLattice<cal::FpConstantRoundingLattice>);

// Each sub-axis lattice is NOT a Semiring (the carrier is non-numeric
// chain; Semiring concept requires ⊕/⊗ separate from join/meet — see
// QttSemiring/StalenessSemiring).  Negative coverage:
static_assert(!ca::Semiring<cal::FpRoundingLattice>);
static_assert(!ca::Semiring<cal::FpLibmPolicyLattice>);

// ── (c) Algebraic laws — chain-lattice reflexivity + identity ───────
//
// `bottom() join x == x` and `top() meet x == x` (identity laws), and
// `leq(x, x) == true` (reflexivity).  Pin the load-bearing chain-
// lattice axioms at every endpoint pair so a downstream contributor
// who edits ChainLatticeOps<>::leq/join/meet to a degenerate
// implementation reddens this TU on first build.
static_assert(cal::FpRoundingLattice::join(cal::FpRoundingLattice::bottom(),
                                            cal::FpRounding::RoundToNearestEven)
              == cal::FpRounding::RoundToNearestEven);
static_assert(cal::FpRoundingLattice::meet(cal::FpRoundingLattice::top(),
                                            cal::FpRounding::RoundToNearestEven)
              == cal::FpRounding::RoundToNearestEven);
static_assert(cal::FpRoundingLattice::leq(cal::FpRounding::RoundToNearestEven,
                                           cal::FpRounding::RoundToNearestEven));

static_assert(cal::FpFtzLattice::join(cal::FpFtzLattice::bottom(),
                                       cal::FpFtz::FlushToZero)
              == cal::FpFtz::FlushToZero);
static_assert(cal::FpFtzLattice::meet(cal::FpFtzLattice::top(),
                                       cal::FpFtz::PreserveSubnormals)
              == cal::FpFtz::PreserveSubnormals);

static_assert(cal::FpContractLattice::join(cal::FpContract::Off,
                                            cal::FpContract::Fast)
              == cal::FpContract::Fast);
static_assert(cal::FpTrapMaskLattice::join(cal::FpTrapMask::AllMasked,
                                            cal::FpTrapMask::UnmaskedInexact)
              == cal::FpTrapMask::UnmaskedInexact);
static_assert(cal::FpInfPolicyLattice::leq(cal::FpInfPolicy::PropagateInfinity,
                                            cal::FpInfPolicy::FlushInfToFinite));

// ── (d) Cross-lattice type-distinctness — defense vs typo regressions
//
// The 11 sub-axis lattice STRUCTS are distinct types so the C++ type
// system rejects accidental cross-axis arithmetic (e.g. calling
// FpRoundingLattice::join on FpFtz values).  Mirrors the same check
// for sub-axis ENUMS in the V-088 sentinel.
static_assert(!std::is_same_v<cal::FpRoundingLattice,        cal::FpFtzLattice>);
static_assert(!std::is_same_v<cal::FpContractLattice,        cal::FpReassociateLattice>);
static_assert(!std::is_same_v<cal::FpTrapMaskLattice,        cal::FpNanPolicyLattice>);
static_assert(!std::is_same_v<cal::FpComplexLayoutLattice,   cal::FpLibmPolicyLattice>);
static_assert(!std::is_same_v<cal::FpDenormalInputLattice,   cal::FpInfPolicyLattice>);
static_assert(!std::is_same_v<cal::FpRoundingLattice,        cal::FpConstantRoundingLattice>);

// ── name() returns the lattice's canonical string ───────────────────
//
// Each lattice's `name()` returns its struct name (modulo trailing
// "Lattice").  A regression that returns the sentinel "<unnamed
// lattice>" or an empty string lights this assert.  We pin the
// concrete strings here because the V-088 sentinel only pins the
// enum cardinality, not the lattice display name.
static_assert(cal::FpRoundingLattice::name()
              == std::string_view{"FpRoundingLattice"});
static_assert(cal::FpFtzLattice::name()
              == std::string_view{"FpFtzLattice"});
static_assert(cal::FpContractLattice::name()
              == std::string_view{"FpContractLattice"});
static_assert(cal::FpTrapMaskLattice::name()
              == std::string_view{"FpTrapMaskLattice"});
static_assert(cal::FpDenormalInputLattice::name()
              == std::string_view{"FpDenormalInputLattice"});
static_assert(cal::FpNanPolicyLattice::name()
              == std::string_view{"FpNanPolicyLattice"});
static_assert(cal::FpInfPolicyLattice::name()
              == std::string_view{"FpInfPolicyLattice"});
static_assert(cal::FpComplexLayoutLattice::name()
              == std::string_view{"FpComplexLayoutLattice"});
static_assert(cal::FpLibmPolicyLattice::name()
              == std::string_view{"FpLibmPolicyLattice"});
static_assert(cal::FpReassociateLattice::name()
              == std::string_view{"FpReassociateLattice"});
static_assert(cal::FpConstantRoundingLattice::name()
              == std::string_view{"FpConstantRoundingLattice"});

// ── At<T> singleton sub-lattices EBO-collapse to empty ──────────────
//
// The At<T> idiom (regime-1 zero-cost — see algebra/GradedTrait.h) is
// load-bearing for V-090's safety/Fp*.h wrappers: `Graded<Modality,
// FpRoundingLattice::At<RoundToNearestEven>, T>` collapses to
// `sizeof(T)` only when At<T>::element_type is empty.  Pin that here
// so V-090's `sizeof(Graded) == sizeof(T)` claims aren't a fiction.
static_assert(std::is_empty_v<
    cal::FpRoundingLattice::At<cal::FpRounding::RoundToNearestEven>::element_type>);
static_assert(std::is_empty_v<
    cal::FpFtzLattice::At<cal::FpFtz::FlushToZero>::element_type>);
static_assert(std::is_empty_v<
    cal::FpContractLattice::At<cal::FpContract::Fast>::element_type>);
static_assert(std::is_empty_v<
    cal::FpTrapMaskLattice::At<cal::FpTrapMask::AllMasked>::element_type>);
static_assert(std::is_empty_v<
    cal::FpReassociateLattice::At<cal::FpReassociate::Forbidden>::element_type>);

}  // namespace

int main() {
    // (e) Runtime exercise — non-constant arg lattice ops on every
    // sub-axis.  Catches consteval/SFINAE bugs that pure static_assert
    // (compile-time-only) would mask.  Per
    // feedback_algebra_runtime_smoke_test_discipline memory.
    cal::detail::fp_mode_lattice_self_test::fp_mode_lattice_runtime_smoke_test();
    return 0;
}
