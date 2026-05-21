#pragma once

// ── crucible::safety::Witness<Witness_v Tier, T> ───────────────────
//
// Type-pinned proof-strength wrapper.  A value of type T whose
// epistemic-confidence tier (UNWITNESSED ⊑ TYPE_CHECKED ⊑ TEST_PASSED
// ⊑ FORMALLY_VERIFIED) is fixed at the type level via the non-type
// template parameter Tier.  Third worked example in the chain-lattice
// wrapper family (after NumericalTier and Consistency) per V-053..V-056
// arc — V-053 ships the lattice, V-054 (this header) the Graded
// carrier, V-055 the row_hash specialization, V-056 the fixy alias.
//
//   Substrate: Graded<ModalityKind::Comonad,
//                     WitnessLattice::At<Tier>,
//                     T>
//   Regime:    1 (zero-cost EBO collapse — At<Tier>::element_type is
//                 empty, sizeof(Witness<Tier, T>) == sizeof(T))
//
//   Use case:  V-176 mimic/nv/Kernel.h declares the certified-kernel
//              return type as `Witness<FORMALLY_VERIFIED, CompiledKernel>`
//              — a downstream consumer asking for TEST_PASSED accepts
//              the kernel transparently; a consumer asking for
//              FORMALLY_VERIFIED rejects every lower-tier kernel at
//              the boundary; a consumer asking for UNWITNESSED accepts
//              every kernel (bottom).  More broadly: any production
//              site producing a value backed by mathematical proof,
//              cross-vendor CI matrix validation, or compile-time
//              safety-wrapper discipline declares the tier in the
//              return type so consumer-side floor gates compose
//              compositionally with no runtime check.
//
//   Axiom coverage:
//     TypeSafe — Witness_v is a strong enum (`enum class : uint8_t`);
//                cross-tier mismatches are compile errors via the
//                relax<WeakerTier>() and satisfies<RequiredTier> gates.
//     DetSafe — every operation is constexpr; the tier is a STATIC
//                property of the value, so cross-vendor CI's
//                BITEXACT_STRICT recipe-tier matrix validates
//                per-tier invariants identically across hardware.
//     MemSafe — defaulted copy/move; T's move semantics carry
//                through.  Witness IS COPYABLE — the witness is
//                metadata about the value's proof strength, NOT a
//                classified-information channel restricting
//                duplication (cf. Secret which deletes copy).
//     InitSafe — NSDMI on impl_ via Graded's substrate.
//   Runtime cost:
//     sizeof(Witness<Tier, T>) == sizeof(T).  Verified by
//     CRUCIBLE_GRADED_LAYOUT_INVARIANT below.  At<Tier>::element_type
//     is empty; Graded's [[no_unique_address]] grade_ EBO-collapses;
//     the wrapper is byte-equivalent to the bare T at -O3.
//
// ── Why Modality::Comonad ───────────────────────────────────────────
//
// A witness tier encodes "the producer had this much proof when the
// value was made."  The substrate's natural Comonad-counit operation
// is `extract`: observe the witnessed value as plain T.  This is
// sound BY DESIGN — the witness adds CONFIDENCE about the value's
// invariant, not RESTRICTION on observation.  A consumer reading a
// `Witness<FORMALLY_VERIFIED, T>` is observing the SAME T bytes a
// consumer reading a `Witness<UNWITNESSED, T>` would observe; the
// only difference is the type-level claim about the producer's
// epistemic confidence.
//
// This is the dual of Secret<T>: Secret's Comonad-counit is GATED
// via the `secret_policy::*` declassify rail (information-flow exit
// restriction); Witness's Comonad-counit is UNGATED (observation is
// sound by construction).  The asymmetry encodes the substrate's
// proof-strength contract — minting at tier T requires producing the
// proof (the burden is on the producer); observing the witnessed
// value is free (the burden is NOT on the consumer).
//
// ── Why Comonad over Absolute ───────────────────────────────────────
//
// Witness could plausibly be Absolute — like Consistency, the tier
// is a static property pinned in the type.  Why pick Comonad?  Two
// reasons:
//   1. The Comonad-counit `extract` makes the "witnessed → raw" path
//      a NAMED substrate operation, parallel to Secret's declassify
//      and distinct from `consume` (which is shared by all
//      modalities).  Production sites reading a Witness<W, T> for
//      its value can write `.extract()` and grep finds every
//      witness-erasing call site.
//   2. V-053's WitnessLattice ALREADY declares Modality::Comonad in
//      its docstring (lines 71-83); switching here would diverge the
//      lattice's stated modality from the carrier's actual choice
//      with no benefit.
//
// Comonad + empty grade (At<Tier>::element_type is empty) satisfies
// the refined Graded gate `(AbsoluteModality || empty grade)`, so
// `peek_mut()` and `swap()` remain available — mutating T cannot
// violate the tier pin (the tier is a TYPE-LEVEL fact about the
// producer's claim, not about the value's bytes).
//
// ── Tier-conversion API: relax + satisfies ─────────────────────────
//
// Witness subsumption-direction (per WitnessLattice.h L51-69):
//
//   leq(weak, strong) reads "the weaker tier is subsumed by the
//   stronger tier."  Bottom = UNWITNESSED (weakest); Top =
//   FORMALLY_VERIFIED (strongest).
//
// For USE, the direction is REVERSED:
//
//   A producer at a HIGHER tier (FORMALLY_VERIFIED) satisfies a
//   consumer at a LOWER tier (UNWITNESSED / TYPE_CHECKED /
//   TEST_PASSED).  Stronger proof serves weaker requirement.  A
//   Witness<FORMALLY_VERIFIED, T> can be relaxed to
//   Witness<TYPE_CHECKED, T> — the formally-verified value is
//   trivially type-checked.
//
//   The converse is forbidden: a Witness<TYPE_CHECKED, T> CANNOT
//   become a Witness<FORMALLY_VERIFIED, T> — the type-checked value
//   does NOT meet the stricter proof requirement.  No `tighten()`
//   method exists; the only way to obtain a higher-tier witness is
//   to construct one at a SITE that actually has the proof (e.g.,
//   a Mimic kernel emit_kernel path that has the small-SMT discharge
//   in hand, or a cross-vendor CI matrix run that has the pairwise
//   comparison against the CPU oracle).
//
// API:
//
//   - extract() &&                 — Comonad counit; returns T,
//                                    erases the tier.  The named
//                                    "witnessed → raw" path.
//   - peek() / peek_mut() / consume() — same as every Graded-backed
//                                    wrapper; observe / mutate / move.
//   - relax<WeakerTier>() &  / &&  — convert to a less-strict tier;
//                                    compile error if WeakerTier >
//                                    Tier.
//   - satisfies<RequiredTier>      — static predicate: does this
//                                    wrapper's pinned tier subsume
//                                    the required tier?  Equivalent
//                                    to leq(RequiredTier, Tier).
//   - tier (static constexpr)      — the pinned Witness_v value.
//                                    Spelled `tier` (not `level`) per
//                                    the witness-vs-level vocabulary
//                                    in the WitnessLattice docstring.
//
// `Graded::weaken` on the substrate goes UP the lattice (stronger
// proof) — that operation has no meaningful semantics for a
// type-pinned tier and is hidden by the wrapper.  The wrapper
// exposes only relax (DOWN the lattice; weaker tier still served
// by the stronger value).
//
// See V-053 (#1931, WitnessLattice.h) for the underlying lattice;
// V-055 (#1933) for the row_hash specialization; V-056 (#1934) for
// the fixy::wrap::Witness re-export; V-176 (#2054) for the first
// production call site (mimic/nv/Kernel.h certified-kernel return).
//
// ── §XXI Universal Mint factory ─────────────────────────────────────
//
// `mint_witness<Tier, T>(args...)` synthesizes a `Witness<Tier, T>`
// at the §XXI grep-discoverable boundary.  Per CLAUDE.md §XXI: every
// authorization factory is named `mint_<noun>` so `grep "mint_"`
// finds every site that explicitly opts into the tier-level proof
// claim.  Constructing `Witness<Tier, T>{value}` directly is
// functionally equivalent — both gate on `std::is_constructible_v<
// T, Args...>` — the §XXI mint exists for grep-discoverability AND
// because witness mints are the SOUND moment where the producer
// must have already discharged the proof obligation (the substrate
// cannot verify this; the discipline is at the producer).
//
// HS14 gate: two HS14 neg-compile fixtures at test/safety_neg/
// witness the gate fires across distinct mismatch classes:
//   1. relax-to-stronger — neg_witness_relax_to_stronger.cpp
//      witnesses the relax<StrongerTier>() requires-clause rejects
//      the upward conversion.
//   2. wrong-arg-type — neg_witness_mint_wrong_arg.cpp witnesses
//      mint_witness<TIER, T>(bad_arg) requires-clause rejects when
//      T is not constructible from the supplied args.

#include <crucible/Platform.h>
#include <crucible/algebra/Graded.h>
#include <crucible/algebra/lattices/WitnessLattice.h>

#include <cstdlib>      // std::abort in the runtime smoke test
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::safety {

// Hoist the WitnessLattice enum into the safety:: namespace so call
// sites don't need to spell `algebra::lattices::Witness::FORMALLY_VERIFIED`.
// Matches Consistency.h's `Consistency_v` aliasing — the class
// template `Witness<Tier, T>` shadows the lattice's `Witness` enum
// in `safety::` scope, so we re-export the enum under the unambiguous
// alias `Witness_v` ("witness value").  Production call sites write
// `Witness<Witness_v::FORMALLY_VERIFIED, T>` for clarity; the alias
// namespace `witness::FormallyVerified<T>` etc. provides shorter
// forms for common cases.
using ::crucible::algebra::lattices::WitnessLattice;
using Witness_v = ::crucible::algebra::lattices::Witness;

template <Witness_v Tier, typename T>
class [[nodiscard]] Witness {
public:
    // ── Public type aliases ─────────────────────────────────────────
    using value_type   = T;
    using lattice_type = WitnessLattice::At<Tier>;
    using graded_type  = ::crucible::algebra::Graded<
        ::crucible::algebra::ModalityKind::Comonad,
        lattice_type,
        T>;
    static constexpr ::crucible::algebra::ModalityKind modality =
        ::crucible::algebra::ModalityKind::Comonad;

    // The pinned tier — exposed as a static constexpr for callers
    // doing tier-aware dispatch without instantiating the wrapper.
    // Spelled `tier` (not `level`) to match WitnessLattice.h's
    // proof-strength vocabulary (cf. ConsistencyLattice's `level`
    // for distributed-consistency tiers).
    static constexpr Witness_v tier = Tier;

private:
    // Empty-lattice element_type collapses via [[no_unique_address]]
    // in Graded; impl_ is byte-equivalent to T at -O3.
    graded_type impl_;

public:

    // ── Construction ────────────────────────────────────────────────
    //
    // Default: T{} at the pinned tier.
    //
    // SEMANTIC NOTE: a default-constructed Witness<FORMALLY_VERIFIED,
    // T> claims its T{} bytes carry a formal-verification witness.
    // For trivially-zero T (int{} == 0, double{} == 0.0) this is
    // vacuously a metadata claim about the producer's process; for
    // non-trivial T, the claim becomes meaningful only if the
    // wrapper is constructed in a context that genuinely has the
    // proof in hand.  Production callers SHOULD prefer the
    // explicit-T constructor (or mint_witness factory) at sites
    // that have actually discharged the proof obligation — the
    // default ctor exists for compatibility with std::array<
    // Witness<...>, N> / struct-field default-init contexts.
    constexpr Witness() noexcept(
        std::is_nothrow_default_constructible_v<T>)
        : impl_{T{}, typename lattice_type::element_type{}} {}

    // Explicit construction from a T value.  The most common
    // production pattern — a proof-producing site produces a value
    // alongside the discharge witness; the wrapper binds that tier
    // into the type.
    constexpr explicit Witness(T value) noexcept(
        std::is_nothrow_move_constructible_v<T>)
        : impl_{std::move(value), typename lattice_type::element_type{}} {}

    // In-place construction — avoids moving T through a temporary.
    // Mirrors Consistency.h / Secret.h / NumericalTier's std::in_place_t
    // pattern.
    template <typename... Args>
        requires std::is_constructible_v<T, Args...>
    constexpr explicit Witness(std::in_place_t, Args&&... args)
        noexcept(std::is_nothrow_constructible_v<T, Args...>
                 && std::is_nothrow_move_constructible_v<T>)
        : impl_{T(std::forward<Args>(args)...),
                typename lattice_type::element_type{}} {}

    // Defaulted copy/move/destroy — Witness IS COPYABLE.  A tier
    // pin is a static property of the value's proof strength;
    // copying a value copies its witness claim unchanged.  This
    // is the OPPOSITE of Secret<T>, which deletes copy to enforce
    // information-flow non-duplication — witness IS metadata, not
    // a classified channel.
    constexpr Witness(const Witness&)            = default;
    constexpr Witness(Witness&&)                 = default;
    constexpr Witness& operator=(const Witness&) = default;
    constexpr Witness& operator=(Witness&&)      = default;
    ~Witness()                                   = default;

    // Equality: compares value bytes within the SAME tier pin.
    // Cross-tier comparison is rejected at overload resolution
    // because the friend takes two `Witness const&` of identical
    // <Tier, T> instantiation.  Mirrors Consistency.h's family-
    // parity discipline.
    [[nodiscard]] friend constexpr bool operator==(
        Witness const& a, Witness const& b) noexcept(
        noexcept(a.peek() == b.peek()))
        requires requires(T const& x, T const& y) { { x == y } -> std::convertible_to<bool>; }
    {
        return a.peek() == b.peek();
    }

    // ── Diagnostic names (forwarded from Graded substrate) ─────────
    //
    // value_type_name(): T's display string via P2996 reflection.
    // lattice_name():    "WitnessLattice::At<FORMALLY_VERIFIED>" etc.
    [[nodiscard]] static consteval std::string_view value_type_name() noexcept {
        return graded_type::value_type_name();
    }
    [[nodiscard]] static consteval std::string_view lattice_name() noexcept {
        return graded_type::lattice_name();
    }

    // ── Read-only access ────────────────────────────────────────────
    [[nodiscard]] constexpr T const& peek() const& noexcept {
        return impl_.peek();
    }

    [[nodiscard]] constexpr T consume() &&
        noexcept(std::is_nothrow_move_constructible_v<T>)
    {
        return std::move(impl_).consume();
    }

    // ── Mutable access ──────────────────────────────────────────────
    //
    // peek_mut forwards through Graded::peek_mut — admitted by the
    // refined gate `(AbsoluteModality || empty grade)`: At<Tier>::
    // element_type is empty for every Tier, so Comonad+empty admits
    // the operation.  Mutating T cannot violate the tier pin: the
    // tier is a TYPE-LEVEL fact about how the value was produced,
    // not about the value's current bytes.
    [[nodiscard]] constexpr T& peek_mut() & noexcept {
        return impl_.peek_mut();
    }

    // ── extract — Comonad counit ────────────────────────────────────
    //
    // The named "witnessed → raw" path.  Consumes the Witness and
    // returns the underlying T; the tier metadata is erased.
    // Unrestricted (unlike Secret<T>::declassify, which requires a
    // policy tag) — observing a witnessed value as plain T is sound
    // because the witness adds CONFIDENCE about the producer's
    // proof, not RESTRICTION on observation.  Forwards through
    // Graded::extract().
    //
    // Symmetric naming with Secret::declassify (also Comonad counit,
    // also `&&`-qualified, also returns T): the methods do the same
    // thing at the substrate level; the asymmetry is at the gate —
    // declassify requires a `secret_policy::*` tag; extract is open.
    [[nodiscard]] constexpr T extract() &&
        noexcept(std::is_nothrow_move_constructible_v<T>)
    {
        return std::move(impl_).extract();
    }

    // ── swap (forwarded from Graded substrate) ─────────────────────
    constexpr void swap(Witness& other)
        noexcept(std::is_nothrow_swappable_v<T>)
    {
        impl_.swap(other.impl_);
    }

    friend constexpr void swap(Witness& a, Witness& b)
        noexcept(std::is_nothrow_swappable_v<T>)
    {
        a.swap(b);
    }

    // ── satisfies<RequiredTier> — static subsumption check ─────────
    //
    // True iff this wrapper's pinned tier is at least as strong as
    // RequiredTier.  Implements the lattice direction:
    //
    //   leq(RequiredTier, Tier)  reads
    //   "the required tier is BELOW (or equal to) the pinned tier"
    //
    // — which means the pinned tier subsumes the requirement; the
    // pinned-tier value is admissible at the consumer site.
    //
    // Use:
    //   static_assert(Witness<Witness_v::FORMALLY_VERIFIED, T>
    //                     ::satisfies<Witness_v::TEST_PASSED>);
    //   // ✓ — FORMALLY_VERIFIED subsumes TEST_PASSED
    //
    //   static_assert(!Witness<Witness_v::TYPE_CHECKED, T>
    //                      ::satisfies<Witness_v::FORMALLY_VERIFIED>);
    //   // ✓ — TYPE_CHECKED does NOT subsume FORMALLY_VERIFIED
    template <Witness_v RequiredTier>
    static constexpr bool satisfies = WitnessLattice::leq(RequiredTier, Tier);

    // ── relax<WeakerTier> — convert to a less-strict tier ──────────
    //
    // Returns a Witness<WeakerTier, T> carrying the same value
    // bytes.  Allowed iff WeakerTier ≤ Tier in the lattice (the
    // weaker tier is below or equal to the pinned tier).  A
    // stronger proof still satisfies a weaker requirement.
    //
    // Compile error when WeakerTier > Tier — there's no way to
    // strengthen a tier pin once the value was produced under a
    // weaker proof regime.
    template <Witness_v WeakerTier>
        requires (WitnessLattice::leq(WeakerTier, Tier))
    [[nodiscard]] constexpr Witness<WeakerTier, T> relax() const&
        noexcept(std::is_nothrow_copy_constructible_v<T>)
        requires std::copy_constructible<T>
    {
        return Witness<WeakerTier, T>{this->peek()};
    }

    template <Witness_v WeakerTier>
        requires (WitnessLattice::leq(WeakerTier, Tier))
    [[nodiscard]] constexpr Witness<WeakerTier, T> relax() &&
        noexcept(std::is_nothrow_move_constructible_v<T>)
    {
        return Witness<WeakerTier, T>{
            std::move(impl_).consume()};
    }
};

// ── §XXI Universal Mint factory ─────────────────────────────────────
//
// Token mint (no Ctx) per §XXI table.  Witness is type-tagged by
// `Tier`; the mint forwards constructor args through the in-place
// path, the requires-clause gates on `std::is_constructible_v<T,
// Args...>` (load-bearing soundness check).
template <Witness_v Tier, typename T, typename... Args>
    requires std::is_constructible_v<T, Args...>
[[nodiscard]] constexpr Witness<Tier, T> mint_witness(Args&&... args)
    noexcept(std::is_nothrow_constructible_v<T, Args...>)
{
    return Witness<Tier, T>{std::in_place, std::forward<Args>(args)...};
}

// ── Convenience aliases ─────────────────────────────────────────────
//
// Per-tier aliases for the most common production sites.  Mirrors
// the consistency:: namespace in Consistency.h.
//
// NAMING NOTE: the namespace is `witness_tier` (not bare `witness`)
// because the FIXY-G9 proof-relevance metasystem at
// `safety::witness::` already exposes `Asserted`, `Tested`,
// `CrossValidated`, `FormallyVerified` as empty tag structs in the
// witness:: namespace.  Our convenience aliases want the SAME English
// noun (`FormallyVerified`) but parametrize T, not the proof-cert.
// `witness_tier::` keeps the V-054 carrier's tier-pinned shortcuts
// separate from the G9 metasystem's proof-strength tags while
// preserving the parallel-vocabulary intent.
namespace witness_tier {
    template <typename T> using Unwitnessed       = Witness<Witness_v::UNWITNESSED,       T>;
    template <typename T> using TypeChecked       = Witness<Witness_v::TYPE_CHECKED,      T>;
    template <typename T> using TestPassed        = Witness<Witness_v::TEST_PASSED,       T>;
    template <typename T> using FormallyVerified  = Witness<Witness_v::FORMALLY_VERIFIED, T>;
}  // namespace witness_tier

// ── Layout invariants ───────────────────────────────────────────────
//
// regime-1: zero-cost EBO collapse.  sizeof(Witness<Tier, T>) ==
// sizeof(T) at every supported tier.  Witnessed at three T sizes
// (1B, 4B, 8B) and across the full tier spectrum.
namespace detail::witness_layout {

template <typename T> using FormallyW = Witness<Witness_v::FORMALLY_VERIFIED, T>;
template <typename T> using TestedW   = Witness<Witness_v::TEST_PASSED,       T>;
template <typename T> using TypedW    = Witness<Witness_v::TYPE_CHECKED,      T>;
template <typename T> using UnwitW    = Witness<Witness_v::UNWITNESSED,       T>;

CRUCIBLE_GRADED_LAYOUT_INVARIANT(FormallyW, char);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(FormallyW, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(FormallyW, double);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(TestedW,   int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(TestedW,   double);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(TypedW,    int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(UnwitW,    int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(UnwitW,    double);

}  // namespace detail::witness_layout

// Direct sizeof witnesses — EBO collapse must hold for the
// production-typical T sizes regardless of which tier is pinned.
static_assert(sizeof(Witness<Witness_v::UNWITNESSED,       int>)    == sizeof(int));
static_assert(sizeof(Witness<Witness_v::TYPE_CHECKED,      int>)    == sizeof(int));
static_assert(sizeof(Witness<Witness_v::TEST_PASSED,       int>)    == sizeof(int));
static_assert(sizeof(Witness<Witness_v::FORMALLY_VERIFIED, int>)    == sizeof(int));
static_assert(sizeof(Witness<Witness_v::FORMALLY_VERIFIED, double>) == sizeof(double));
static_assert(sizeof(Witness<Witness_v::FORMALLY_VERIFIED, char>)   == sizeof(char));

// ── Self-test ───────────────────────────────────────────────────────
namespace detail::witness_self_test {

using FormallyInt   = Witness<Witness_v::FORMALLY_VERIFIED, int>;
using TestedInt     = Witness<Witness_v::TEST_PASSED,       int>;
using TypedInt      = Witness<Witness_v::TYPE_CHECKED,      int>;
using UnwitnessedInt = Witness<Witness_v::UNWITNESSED,      int>;

// ── Construction paths ─────────────────────────────────────────────
inline constexpr FormallyInt w_default{};
static_assert(w_default.peek() == 0);
static_assert(w_default.tier == Witness_v::FORMALLY_VERIFIED);

inline constexpr FormallyInt w_explicit{42};
static_assert(w_explicit.peek() == 42);

// ── Pinned tier accessor ───────────────────────────────────────────
static_assert(FormallyInt::tier    == Witness_v::FORMALLY_VERIFIED);
static_assert(TestedInt::tier      == Witness_v::TEST_PASSED);
static_assert(TypedInt::tier       == Witness_v::TYPE_CHECKED);
static_assert(UnwitnessedInt::tier == Witness_v::UNWITNESSED);

// ── satisfies<RequiredTier> — subsumption-up direction ─────────────
//
// A FORMALLY_VERIFIED producer satisfies every consumer.
static_assert(FormallyInt::satisfies<Witness_v::FORMALLY_VERIFIED>);
static_assert(FormallyInt::satisfies<Witness_v::TEST_PASSED>);
static_assert(FormallyInt::satisfies<Witness_v::TYPE_CHECKED>);
static_assert(FormallyInt::satisfies<Witness_v::UNWITNESSED>);

// A TEST_PASSED producer satisfies weaker-or-equal consumers only.
static_assert( TestedInt::satisfies<Witness_v::TEST_PASSED>);  // self
static_assert( TestedInt::satisfies<Witness_v::TYPE_CHECKED>); // weaker
static_assert( TestedInt::satisfies<Witness_v::UNWITNESSED>);
static_assert(!TestedInt::satisfies<Witness_v::FORMALLY_VERIFIED>); // stronger

// A TYPE_CHECKED producer satisfies TYPE_CHECKED and UNWITNESSED only.
static_assert( TypedInt::satisfies<Witness_v::TYPE_CHECKED>);
static_assert( TypedInt::satisfies<Witness_v::UNWITNESSED>);
static_assert(!TypedInt::satisfies<Witness_v::TEST_PASSED>);
static_assert(!TypedInt::satisfies<Witness_v::FORMALLY_VERIFIED>);

// An UNWITNESSED producer satisfies only UNWITNESSED consumers.
static_assert( UnwitnessedInt::satisfies<Witness_v::UNWITNESSED>);
static_assert(!UnwitnessedInt::satisfies<Witness_v::TYPE_CHECKED>);
static_assert(!UnwitnessedInt::satisfies<Witness_v::TEST_PASSED>);
static_assert(!UnwitnessedInt::satisfies<Witness_v::FORMALLY_VERIFIED>);

// ── relax<WeakerTier> — DOWN-the-lattice conversion ────────────────
//
// FORMALLY_VERIFIED relaxes to any tier.
inline constexpr auto from_formally_to_tested =
    FormallyInt{42}.relax<Witness_v::TEST_PASSED>();
static_assert(from_formally_to_tested.peek() == 42);
static_assert(from_formally_to_tested.tier == Witness_v::TEST_PASSED);

inline constexpr auto from_formally_to_unwitnessed =
    FormallyInt{99}.relax<Witness_v::UNWITNESSED>();
static_assert(from_formally_to_unwitnessed.peek() == 99);
static_assert(from_formally_to_unwitnessed.tier == Witness_v::UNWITNESSED);

// TEST_PASSED relaxes to TYPE_CHECKED.
inline constexpr auto from_tested_to_typed =
    TestedInt{7}.relax<Witness_v::TYPE_CHECKED>();
static_assert(from_tested_to_typed.peek() == 7);
static_assert(from_tested_to_typed.tier == Witness_v::TYPE_CHECKED);

// Reflexive: relax<SameTier> is a no-op.
inline constexpr auto identity_relax =
    FormallyInt{100}.relax<Witness_v::FORMALLY_VERIFIED>();
static_assert(identity_relax.peek() == 100);
static_assert(identity_relax.tier == Witness_v::FORMALLY_VERIFIED);

// ── mint_witness §XXI factory ──────────────────────────────────────
inline constexpr auto minted_formally =
    mint_witness<Witness_v::FORMALLY_VERIFIED, int>(123);
static_assert(minted_formally.peek() == 123);
static_assert(minted_formally.tier == Witness_v::FORMALLY_VERIFIED);

// ── Convenience aliases round-trip ─────────────────────────────────
static_assert(std::is_same_v<witness_tier::FormallyVerified<int>, FormallyInt>);
static_assert(std::is_same_v<witness_tier::TestPassed<int>,        TestedInt>);
static_assert(std::is_same_v<witness_tier::TypeChecked<int>,       TypedInt>);
static_assert(std::is_same_v<witness_tier::Unwitnessed<int>,       UnwitnessedInt>);

// ── Equality at same tier ──────────────────────────────────────────
static_assert(FormallyInt{42} == FormallyInt{42});
static_assert(!(FormallyInt{42} == FormallyInt{43}));

// ── Copyability — Witness IS COPYABLE (unlike Secret) ──────────────
static_assert(std::is_copy_constructible_v<FormallyInt>);
static_assert(std::is_copy_assignable_v<FormallyInt>);
static_assert(std::is_move_constructible_v<FormallyInt>);
static_assert(std::is_move_assignable_v<FormallyInt>);

// ── Modality declaration ───────────────────────────────────────────
static_assert(FormallyInt::modality == ::crucible::algebra::ModalityKind::Comonad);

// ── Runtime smoke test ──────────────────────────────────────────────
//
// Exercise construction / peek / extract / relax / mint_witness with
// non-constant arguments per the algebra/* runtime-smoke-test
// discipline (feedback_algebra_runtime_smoke_test_discipline).
// Critical because Witness routes through Graded::extract() (the
// Comonad counit) for extract(); that path is distinct from peek()
// / consume() and a constexpr-vs-runtime divergence would silently
// misroute witness reads.
inline void runtime_smoke_test() {
    int seed = 17;                                          // non-constant

    FormallyInt w{seed * 2};
    if (w.peek() != 34) std::abort();
    if (w.tier != Witness_v::FORMALLY_VERIFIED) std::abort();

    // peek_mut — Comonad+empty admits mutation.
    w.peek_mut() = 99;
    if (w.peek() != 99) std::abort();

    // extract — Comonad counit, unrestricted.
    FormallyInt e{seed * 3};
    int extracted = std::move(e).extract();
    if (extracted != 51) std::abort();

    // relax — stronger → weaker.
    FormallyInt source{seed * 4};
    auto relaxed = std::move(source).relax<Witness_v::TEST_PASSED>();
    if (relaxed.peek() != 68) std::abort();
    if (relaxed.tier != Witness_v::TEST_PASSED) std::abort();

    // mint_witness factory path (in-place ctor).
    auto m = mint_witness<Witness_v::TEST_PASSED, int>(seed);
    int m_out = std::move(m).extract();
    if (m_out != 17) std::abort();

    // Copy semantics — Witness IS COPYABLE (metadata, not classified).
    FormallyInt c1{seed};
    FormallyInt c2 = c1;                                    // copy ctor
    if (c1.peek() != c2.peek()) std::abort();
    if (c1.peek() != 17) std::abort();

    // swap — Comonad+empty admits swap.
    FormallyInt a{1}, b{2};
    swap(a, b);
    if (a.peek() != 2 || b.peek() != 1) std::abort();
}

}  // namespace detail::witness_self_test

}  // namespace crucible::safety
