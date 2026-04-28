#pragma once

// ── crucible::safety::NumericalTier<Tolerance T_at, T> ──────────────
//
// Type-pinned numeric-tolerance wrapper.  A value of type T whose
// numerical tier (RELAXED ⊑ ULP_INT8 ⊑ ULP_FP8 ⊑ ULP_FP16 ⊑ ULP_FP32
// ⊑ ULP_FP64 ⊑ BITEXACT) is fixed at the type level via the
// non-type template parameter T_at.  First production wrapper from
// the 28_04_2026_effects.md §4.2.1 catalog (FOUND-G01) — the
// worked-example proving the §4.1 wrapper-author template ships
// end-to-end.
//
//   Substrate: Graded<ModalityKind::Absolute,
//                     ToleranceLattice::At<T_at>,
//                     T>
//   Regime:    1 (zero-cost EBO collapse — At<T_at>::element_type is
//                 empty, sizeof(NumericalTier<T_at, T>) == sizeof(T))
//
//   Use case:  §10 precision-budget calibrator from 25_04_2026.md +
//              FORGE.md §20 NumericalRecipe registry.  A kernel
//              callable signature carrying NumericalTier<BITEXACT_TC,
//              ResultTensor> at compile time refuses to be invoked
//              from a NumericalTier<RELAXED, ...> caller, fencing
//              the "BITEXACT_STRICT consumer accidentally invokes
//              UNORDERED reduction" bug class at the call site
//              instead of in the cross-vendor numerics CI 12 hours
//              later.
//
//   Axiom coverage:
//     TypeSafe — Tolerance is a strong enum (`enum class : uint8_t`);
//                cross-tier mismatches are compile errors via the
//                relax<LooserTier>() and satisfies<RequiredTier>
//                gates.
//     DetSafe — every operation is constexpr; the tier is a STATIC
//                property of the value, so cross-vendor numerics CI
//                can validate per-tier ULP bounds (MIMIC.md §41).
//     MemSafe — defaulted copy/move; T's move semantics carry through.
//     InitSafe — NSDMI on impl_ via Graded's substrate.
//   Runtime cost:
//     sizeof(NumericalTier<T_at, T>) == sizeof(T).  Verified by
//     CRUCIBLE_GRADED_LAYOUT_INVARIANT below.  At<T_at>::element_type
//     is empty; Graded's [[no_unique_address]] grade_ EBO-collapses;
//     the wrapper is byte-equivalent to the bare T at -O3.
//
// ── Why Modality::Absolute, not Comonad ─────────────────────────────
//
// A tier pin is a STATIC property of the value's bytes — "this
// tensor was computed under BITEXACT_TC discipline" — not a context
// the value lives in.  Compare to Secret<T> (Comonad — declassify
// extracts FROM the classified context; the value's bytes don't
// inherently carry the classification, the wrapper does) and
// Tagged<T, Source> (RelativeMonad — the source is provenance
// metadata flowing alongside the value).  NumericalTier is closer
// to Linear (Absolute, QttSemiring) — the grade describes the
// value's intrinsic property, mutation of T can't violate it
// (changing the bytes doesn't change the recipe that produced
// them; the wrapper is a TYPE-LEVEL CERTIFICATE, not a runtime
// classification).
//
// ── Tier-conversion API: relax + satisfies ─────────────────────────
//
// Tolerance subsumption-direction (per ToleranceLattice.h L40-58):
//
//   leq(loose, tight) reads "loose is below tight in the lattice."
//   Bottom = RELAXED (loosest); Top = BITEXACT (tightest).
//
// For USE, the direction is REVERSED:
//
//   A producer at a HIGHER tier (BITEXACT) satisfies a consumer at
//   a LOWER tier (ULP_FP16).  Stronger promise serves weaker
//   requirement.  A NumericalTier<BITEXACT, T> can be relaxed to
//   NumericalTier<ULP_FP16, T> — the BITEXACT-computed value is
//   trivially ULP_FP16-compliant.
//
//   The converse is forbidden: a NumericalTier<ULP_FP16, T> CANNOT
//   become a NumericalTier<BITEXACT, T> — the looser computation
//   does NOT meet the stricter contract.  No `tighten()` method
//   exists; the only way to obtain a NumericalTier<BITEXACT, T> is
//   to construct one at the BITEXACT site (e.g., a kernel emit
//   under a BITEXACT_STRICT recipe).
//
// API:
//
//   - relax<LooserTier>() &  / && — convert to a less-strict tier;
//                                   compile error if LooserTier > T_at.
//   - satisfies<RequiredTier>     — static predicate: does this
//                                   wrapper's pinned tier subsume
//                                   the required tier?  Equivalent
//                                   to leq(RequiredTier, T_at).
//   - tier (static constexpr)     — the pinned Tolerance value.
//
// `Graded::weaken` on the substrate goes UP the lattice (stronger
// promise) — that operation has no meaningful semantics for a
// type-pinned tier and is hidden by the wrapper.  The wrapper
// exposes only relax (DOWN the lattice; weaker promise still
// served by the stronger value).
//
// See ALGEBRA-14 (#459, ToleranceLattice.h) for the underlying
// substrate; 28_04_2026_effects.md §4.2.1 for the FOUND-G01 spec
// and the production-call-site rationale; FORGE.md §20 / MIMIC.md
// §41 for the NumericalRecipe registry + cross-vendor numerics CI.

#include <crucible/Platform.h>
#include <crucible/algebra/Graded.h>
#include <crucible/algebra/lattices/ToleranceLattice.h>

#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::safety {

// Hoist the Tolerance enum into the safety:: namespace so call sites
// don't need to spell `algebra::lattices::Tolerance::BITEXACT` —
// matching the convention from Secret.h (which hoists Conf via
// secret_policy::*) and TimeOrdered.h.
using ::crucible::algebra::lattices::Tolerance;
using ::crucible::algebra::lattices::ToleranceLattice;

template <Tolerance T_at, typename T>
class [[nodiscard]] NumericalTier {
public:
    // ── Public type aliases ─────────────────────────────────────────
    using value_type   = T;
    using lattice_type = ToleranceLattice::At<T_at>;
    using graded_type  = ::crucible::algebra::Graded<
        ::crucible::algebra::ModalityKind::Absolute,
        lattice_type,
        T>;
    static constexpr ::crucible::algebra::ModalityKind modality =
        ::crucible::algebra::ModalityKind::Absolute;

    // The pinned tier — exposed as a static constexpr for callers
    // doing tier-aware dispatch without instantiating the wrapper.
    static constexpr Tolerance tier = T_at;

private:
    // Empty-lattice element_type collapses via [[no_unique_address]]
    // in Graded; impl_ is byte-equivalent to T at -O3.
    graded_type impl_;

public:

    // ── Construction ────────────────────────────────────────────────
    //
    // Default: T{} at the pinned tier.  The pinned tier is a type-
    // level fact, not a per-instance one; default construction does
    // not need a tier argument.
    constexpr NumericalTier() noexcept(
        std::is_nothrow_default_constructible_v<T>)
        : impl_{T{}, typename lattice_type::element_type{}} {}

    // Explicit construction from a T value.  The most common
    // production pattern — a kernel emit produces a value under the
    // declared recipe tier; the wrapper binds that tier into the type.
    constexpr explicit NumericalTier(T value) noexcept(
        std::is_nothrow_move_constructible_v<T>)
        : impl_{std::move(value), typename lattice_type::element_type{}} {}

    // In-place construction — avoids moving T through a temporary.
    // Mirrors Secret<T>'s std::in_place_t pattern.
    template <typename... Args>
        requires std::is_constructible_v<T, Args...>
    constexpr explicit NumericalTier(std::in_place_t, Args&&... args)
        noexcept(std::is_nothrow_constructible_v<T, Args...>
                 && std::is_nothrow_move_constructible_v<T>)
        : impl_{T(std::forward<Args>(args)...),
                typename lattice_type::element_type{}} {}

    // Defaulted copy/move/destroy — NumericalTier IS COPYABLE.  A
    // tier pin is a static property of the value; copying a value
    // copies its tier promise unchanged.  Contrast with Linear<T>
    // (which deletes copy because the value identity IS the
    // ownership) — NumericalTier's invariant survives copying.
    constexpr NumericalTier(const NumericalTier&)            = default;
    constexpr NumericalTier(NumericalTier&&)                 = default;
    constexpr NumericalTier& operator=(const NumericalTier&) = default;
    constexpr NumericalTier& operator=(NumericalTier&&)      = default;
    ~NumericalTier()                                         = default;

    // ── Diagnostic names (forwarded from Graded substrate) ─────────
    //
    // value_type_name(): T's display string via P2996 reflection.
    // lattice_name():    "ToleranceLattice::At<BITEXACT>" etc. — the
    //                    pinned-tier sub-lattice's hand-written name.
    //
    // Cross-wrapper parity discipline (audit Tier-2): both forwarders
    // forward verbatim from graded_type's, so the GradedWrapper
    // concept's CHEAT-3 fidelity check passes.
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
    // Absolute modality gate.  Mutating T cannot violate the tier
    // pin: the tier is a TYPE-LEVEL fact about how the value was
    // produced, not about the value's current bytes.  A caller that
    // mutates a NumericalTier<BITEXACT, std::array<float, N>>'s
    // contents post-construction is asserting the new bytes also
    // satisfy BITEXACT — the discipline lives at the call site, the
    // wrapper trusts it.
    [[nodiscard]] constexpr T& peek_mut() & noexcept {
        return impl_.peek_mut();
    }

    // ── swap (forwarded from Graded substrate) ─────────────────────
    //
    // Standard exchange — swaps T values between two NumericalTier
    // instances pinned at the SAME tier.  Cross-tier swap is a
    // compile error (the types differ) — that's the point.
    constexpr void swap(NumericalTier& other)
        noexcept(std::is_nothrow_swappable_v<T>)
    {
        impl_.swap(other.impl_);
    }

    friend constexpr void swap(NumericalTier& a, NumericalTier& b)
        noexcept(std::is_nothrow_swappable_v<T>)
    {
        a.swap(b);
    }

    // ── satisfies<RequiredTier> — static subsumption check ─────────
    //
    // True iff this wrapper's pinned tier is at least as strict as
    // RequiredTier.  Implements the lattice direction:
    //
    //   leq(RequiredTier, T_at)  reads
    //   "the required tier is BELOW (or equal to) the pinned tier"
    //
    // — which means the pinned tier subsumes the requirement; the
    // pinned-tier value is admissible at the consumer site.
    //
    // Use:
    //   static_assert(NumericalTier<Tolerance::BITEXACT, T>
    //                     ::satisfies<Tolerance::ULP_FP16>);
    //   // ✓ — BITEXACT subsumes ULP_FP16
    //
    //   static_assert(!NumericalTier<Tolerance::ULP_FP16, T>
    //                     ::satisfies<Tolerance::BITEXACT>);
    //   // ✓ — ULP_FP16 does NOT subsume BITEXACT
    template <Tolerance RequiredTier>
    static constexpr bool satisfies = ToleranceLattice::leq(RequiredTier, T_at);

    // ── relax<LooserTier> — convert to a less-strict tier ──────────
    //
    // Returns a NumericalTier<LooserTier, T> carrying the same value
    // bytes.  Allowed iff LooserTier ≤ T_at in the lattice (the
    // looser tier is below or equal to the pinned tier).  A stronger
    // promise still satisfies a weaker requirement.
    //
    // Compile error when LooserTier > T_at — there's no way to
    // strengthen a tier pin once the value was produced under a
    // looser recipe.
    //
    // Two overloads (const& / &&) mirror Stale's combine_max pattern
    // — the const& form copies T; the && form moves it.
    template <Tolerance LooserTier>
        requires (ToleranceLattice::leq(LooserTier, T_at))
    [[nodiscard]] constexpr NumericalTier<LooserTier, T> relax() const&
        noexcept(std::is_nothrow_copy_constructible_v<T>)
        requires std::copy_constructible<T>
    {
        return NumericalTier<LooserTier, T>{this->peek()};
    }

    template <Tolerance LooserTier>
        requires (ToleranceLattice::leq(LooserTier, T_at))
    [[nodiscard]] constexpr NumericalTier<LooserTier, T> relax() &&
        noexcept(std::is_nothrow_move_constructible_v<T>)
    {
        return NumericalTier<LooserTier, T>{
            std::move(impl_).consume()};
    }
};

// ── CTAD: deduce T from the value argument; T_at must be explicit ──
//
// Unlike Stale<T> which has a single-arg ctor where T deduces, the
// non-type template parameter T_at must be supplied explicitly:
//
//   NumericalTier<Tolerance::BITEXACT, double> x{3.14};   // explicit
//   // ✗ NumericalTier nt{3.14};   // T_at cannot deduce
//
// This is the right discipline — the tier is a load-bearing fact
// the call site MUST declare.  No deduction guide needed.

// ── Convenience aliases ─────────────────────────────────────────────
//
// Per-tier aliases for the most common production sites.  Mirrors
// the tolerance:: namespace in ToleranceLattice.h.
namespace numerical_tier {
    template <typename T> using Relaxed  = NumericalTier<Tolerance::RELAXED,  T>;
    template <typename T> using Int8     = NumericalTier<Tolerance::ULP_INT8, T>;
    template <typename T> using Fp8      = NumericalTier<Tolerance::ULP_FP8,  T>;
    template <typename T> using Fp16     = NumericalTier<Tolerance::ULP_FP16, T>;
    template <typename T> using Fp32     = NumericalTier<Tolerance::ULP_FP32, T>;
    template <typename T> using Fp64     = NumericalTier<Tolerance::ULP_FP64, T>;
    template <typename T> using Bitexact = NumericalTier<Tolerance::BITEXACT, T>;
}  // namespace numerical_tier

// ── Layout invariants ───────────────────────────────────────────────
//
// regime-1: zero-cost EBO collapse.  sizeof(NumericalTier<T_at, T>)
// == sizeof(T) at every supported tier.  Witnessed at three T sizes
// (1B, 4B, 8B) and across the full tier spectrum to catch any
// per-tier layout drift.
namespace detail::numerical_tier_layout {

template <typename T> using BitexactN = NumericalTier<Tolerance::BITEXACT, T>;
template <typename T> using Fp32N     = NumericalTier<Tolerance::ULP_FP32, T>;
template <typename T> using RelaxedN  = NumericalTier<Tolerance::RELAXED,  T>;

CRUCIBLE_GRADED_LAYOUT_INVARIANT(BitexactN, char);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(BitexactN, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(BitexactN, double);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(Fp32N,     int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(Fp32N,     double);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(RelaxedN,  int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(RelaxedN,  double);

}  // namespace detail::numerical_tier_layout

// Direct sizeof witnesses — EBO collapse must hold for the
// production-typical T sizes regardless of which tier is pinned.
static_assert(sizeof(NumericalTier<Tolerance::RELAXED,  int>)    == sizeof(int));
static_assert(sizeof(NumericalTier<Tolerance::ULP_INT8, int>)    == sizeof(int));
static_assert(sizeof(NumericalTier<Tolerance::ULP_FP8,  int>)    == sizeof(int));
static_assert(sizeof(NumericalTier<Tolerance::ULP_FP16, int>)    == sizeof(int));
static_assert(sizeof(NumericalTier<Tolerance::ULP_FP32, int>)    == sizeof(int));
static_assert(sizeof(NumericalTier<Tolerance::ULP_FP64, int>)    == sizeof(int));
static_assert(sizeof(NumericalTier<Tolerance::BITEXACT, int>)    == sizeof(int));
static_assert(sizeof(NumericalTier<Tolerance::BITEXACT, double>) == sizeof(double));

// ── Self-test ───────────────────────────────────────────────────────
namespace detail::numerical_tier_self_test {

using BitexactInt = NumericalTier<Tolerance::BITEXACT, int>;
using Fp16Int     = NumericalTier<Tolerance::ULP_FP16, int>;
using RelaxedInt  = NumericalTier<Tolerance::RELAXED,  int>;

// ── Construction paths ─────────────────────────────────────────────
inline constexpr BitexactInt nt_default{};
static_assert(nt_default.peek() == 0);
static_assert(nt_default.tier == Tolerance::BITEXACT);

inline constexpr BitexactInt nt_explicit{42};
static_assert(nt_explicit.peek() == 42);

// ── Pinned tier accessor ───────────────────────────────────────────
static_assert(BitexactInt::tier == Tolerance::BITEXACT);
static_assert(Fp16Int::tier     == Tolerance::ULP_FP16);
static_assert(RelaxedInt::tier  == Tolerance::RELAXED);

// ── satisfies<RequiredTier> — subsumption-up direction ─────────────
//
// A BITEXACT producer satisfies every consumer.
static_assert(BitexactInt::satisfies<Tolerance::BITEXACT>);
static_assert(BitexactInt::satisfies<Tolerance::ULP_FP64>);
static_assert(BitexactInt::satisfies<Tolerance::ULP_FP32>);
static_assert(BitexactInt::satisfies<Tolerance::ULP_FP16>);
static_assert(BitexactInt::satisfies<Tolerance::ULP_FP8>);
static_assert(BitexactInt::satisfies<Tolerance::ULP_INT8>);
static_assert(BitexactInt::satisfies<Tolerance::RELAXED>);

// An FP16 producer satisfies looser-or-equal consumers only.
static_assert( Fp16Int::satisfies<Tolerance::ULP_FP16>);    // self
static_assert( Fp16Int::satisfies<Tolerance::ULP_FP8>);     // looser
static_assert( Fp16Int::satisfies<Tolerance::ULP_INT8>);
static_assert( Fp16Int::satisfies<Tolerance::RELAXED>);
static_assert(!Fp16Int::satisfies<Tolerance::ULP_FP32>);    // tighter
static_assert(!Fp16Int::satisfies<Tolerance::ULP_FP64>);
static_assert(!Fp16Int::satisfies<Tolerance::BITEXACT>);

// A RELAXED producer satisfies only RELAXED consumers.
static_assert( RelaxedInt::satisfies<Tolerance::RELAXED>);
static_assert(!RelaxedInt::satisfies<Tolerance::ULP_INT8>);
static_assert(!RelaxedInt::satisfies<Tolerance::BITEXACT>);

// ── relax<LooserTier> — DOWN-the-lattice conversion ────────────────
//
// BITEXACT relaxes to any tier.
inline constexpr auto from_bitexact_to_fp16 =
    BitexactInt{42}.relax<Tolerance::ULP_FP16>();
static_assert(from_bitexact_to_fp16.peek() == 42);
static_assert(from_bitexact_to_fp16.tier == Tolerance::ULP_FP16);

inline constexpr auto from_bitexact_to_relaxed =
    BitexactInt{99}.relax<Tolerance::RELAXED>();
static_assert(from_bitexact_to_relaxed.peek() == 99);
static_assert(from_bitexact_to_relaxed.tier == Tolerance::RELAXED);

// FP16 relaxes to FP8 / INT8 / RELAXED but NOT to FP32 / FP64 /
// BITEXACT (those are stricter; the requires-clause refuses).
inline constexpr auto from_fp16_to_int8 =
    Fp16Int{7}.relax<Tolerance::ULP_INT8>();
static_assert(from_fp16_to_int8.peek() == 7);
static_assert(from_fp16_to_int8.tier == Tolerance::ULP_INT8);

inline constexpr auto from_fp16_to_self =
    Fp16Int{8}.relax<Tolerance::ULP_FP16>();   // identity relax (allowed)
static_assert(from_fp16_to_self.peek() == 8);

// SFINAE-style detector: relax<TighterTier> on a looser-pinned
// wrapper must NOT satisfy std::invocable.  This pins the
// requires-clause's correctness — relax DOWN is admitted, relax UP
// is rejected.
template <typename W, Tolerance T_target>
concept can_relax = requires(W w) {
    { std::move(w).template relax<T_target>() };
};

static_assert( can_relax<BitexactInt, Tolerance::ULP_FP16>);   // ✓ down
static_assert( can_relax<BitexactInt, Tolerance::RELAXED>);    // ✓ down
static_assert( can_relax<Fp16Int,     Tolerance::ULP_INT8>);   // ✓ down
static_assert( can_relax<Fp16Int,     Tolerance::ULP_FP16>);   // ✓ self
static_assert(!can_relax<Fp16Int,     Tolerance::ULP_FP32>);   // ✗ up
static_assert(!can_relax<Fp16Int,     Tolerance::BITEXACT>);   // ✗ up
static_assert(!can_relax<RelaxedInt,  Tolerance::ULP_INT8>);   // ✗ up

// ── Diagnostic forwarders ─────────────────────────────────────────
//
// value_type_name forwards via Graded::value_type_name (P2996).
// Per the gcc16_c26_reflection_gotchas memory rule: use ends_with
// rather than == because display_string_of is TU-context-fragile.
static_assert(BitexactInt::value_type_name().ends_with("int"));

// lattice_name forwards via Graded::lattice_name → routes through
// the algebra-level free function lattice_name<L>().  At<T_at>'s
// own name() returns the per-tier hand-written string verbatim.
static_assert(BitexactInt::lattice_name() == "ToleranceLattice::At<BITEXACT>");
static_assert(Fp16Int::lattice_name()     == "ToleranceLattice::At<ULP_FP16>");
static_assert(RelaxedInt::lattice_name()  == "ToleranceLattice::At<RELAXED>");

// ── swap exchanges T values within the same tier pin ──────────────
[[nodiscard]] consteval bool swap_exchanges_within_same_tier() noexcept {
    BitexactInt a{10};
    BitexactInt b{20};
    a.swap(b);
    return a.peek() == 20 && b.peek() == 10;
}
static_assert(swap_exchanges_within_same_tier());

[[nodiscard]] consteval bool free_swap_works() noexcept {
    BitexactInt a{10};
    BitexactInt b{20};
    using std::swap;
    swap(a, b);
    return a.peek() == 20 && b.peek() == 10;
}
static_assert(free_swap_works());

// ── peek_mut allows in-place mutation ─────────────────────────────
[[nodiscard]] consteval bool peek_mut_works() noexcept {
    BitexactInt a{10};
    a.peek_mut() = 99;
    return a.peek() == 99;
}
static_assert(peek_mut_works());

// ── Convenience aliases resolve correctly ────────────────────────
static_assert(numerical_tier::Bitexact<int>::tier == Tolerance::BITEXACT);
static_assert(numerical_tier::Fp32<int>::tier     == Tolerance::ULP_FP32);
static_assert(numerical_tier::Fp16<int>::tier     == Tolerance::ULP_FP16);
static_assert(numerical_tier::Fp8<int>::tier      == Tolerance::ULP_FP8);
static_assert(numerical_tier::Int8<int>::tier     == Tolerance::ULP_INT8);
static_assert(numerical_tier::Relaxed<int>::tier  == Tolerance::RELAXED);

static_assert(std::is_same_v<numerical_tier::Bitexact<double>,
                             NumericalTier<Tolerance::BITEXACT, double>>);

// ── Runtime smoke test ─────────────────────────────────────────────
//
// Per feedback_algebra_runtime_smoke_test_discipline memory: exercise
// every named operation with non-constant arguments at runtime.  Any
// constexpr-vs-runtime divergence in ToleranceLattice's leq/join/
// meet (or in Graded's regime-1 EBO collapse) would surface here
// since the runtime path uses the same constexpr ops but with
// non-folded operands.
inline void runtime_smoke_test() {
    // Construction paths — default + explicit + in_place.
    BitexactInt a{};
    BitexactInt b{42};
    BitexactInt c{std::in_place, 7};

    [[maybe_unused]] auto va = a.peek();
    [[maybe_unused]] auto vb = b.peek();
    [[maybe_unused]] auto vc = c.peek();

    // Static tier accessor — verified at runtime via if() to force
    // the compiler to materialize it (not just const-fold).
    if (BitexactInt::tier != Tolerance::BITEXACT) {
        // Unreachable under correct compilation; kept to defeat
        // dead-store elimination of the static accessor read.
        std::abort();
    }

    // peek_mut — in-place mutation at runtime.
    BitexactInt mutable_b{10};
    mutable_b.peek_mut() = 99;

    // Swap — at runtime to exercise non-constexpr exchange.
    BitexactInt sx{1};
    BitexactInt sy{2};
    sx.swap(sy);

    // Free-function swap (ADL).
    using std::swap;
    swap(sx, sy);

    // relax<LooserTier> — both const& and && overloads.
    BitexactInt source{77};
    auto relaxed_copy = source.relax<Tolerance::ULP_FP16>();
    auto relaxed_move = std::move(source).relax<Tolerance::RELAXED>();
    [[maybe_unused]] auto rcopy = relaxed_copy.peek();
    [[maybe_unused]] auto rmove = relaxed_move.peek();

    // satisfies<...> — runtime-readable static_constexpr predicate.
    [[maybe_unused]] bool s1 = BitexactInt::satisfies<Tolerance::ULP_FP16>;
    [[maybe_unused]] bool s2 = Fp16Int::satisfies<Tolerance::BITEXACT>;

    // Move-construct a new instance from consumed inner.
    BitexactInt orig{55};
    int extracted = std::move(orig).consume();
    if (extracted != 55) std::abort();

    // Convenience-alias instantiation at runtime.
    numerical_tier::Bitexact<int> alias_form{123};
    numerical_tier::Fp16<double>  fp16_form{3.14};
    [[maybe_unused]] auto av = alias_form.peek();
    [[maybe_unused]] auto fv = fp16_form.peek();
}

}  // namespace detail::numerical_tier_self_test

}  // namespace crucible::safety
