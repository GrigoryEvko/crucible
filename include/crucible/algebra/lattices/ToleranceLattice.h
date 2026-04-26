#pragma once

// ── crucible::algebra::lattices::ToleranceLattice ───────────────────
//
// Seven-tier total-order lattice over numeric-tolerance budgets — the
// grading axis underlying §10 precision-budget calibrator from
// 25_04_2026.md (the LP allocator that picks per-op precision recipes
// minimizing total cycles within a user-declared output tolerance).
//
// ── The classification ──────────────────────────────────────────────
//
//     RELAXED       — no tolerance bound.  Quantized inference (INT8/
//                      INT4), low-precision exploration, stochastic
//                      rounding without per-op error tracking.
//     ULP_INT8      — error budget consistent with INT8 quantization
//                      noise (~10⁻²).  GPTQ / AWQ / SmoothQuant
//                      typical post-training quantization tier.
//     ULP_FP8       — error budget consistent with FP8 (E4M3/E5M2)
//                      tensor-core math (~10⁻³ to 10⁻²).  H100/H200
//                      mixed-precision typical.
//     ULP_FP16      — error budget consistent with FP16/BF16 with
//                      FP32 accumulator (~10⁻⁴ to 10⁻³).  Standard
//                      mixed-precision training tier.
//     ULP_FP32      — error budget consistent with single-precision
//                      ULP (~10⁻⁷ to 10⁻⁶).  Default for layers
//                      that need full FP32 (attention LSE, layer
//                      norm reductions).
//     ULP_FP64      — error budget consistent with double-precision
//                      ULP (~10⁻¹⁵ to 10⁻¹⁵).  CPU-side reference,
//                      research-grade simulators.
//     BITEXACT      — zero tolerance.  Bit-equal across replicas /
//                      across hardware classes / across replays.
//                      Required by BITEXACT_STRICT / BITEXACT_TC
//                      NumericalRecipe tiers (MIMIC.md §41).  The
//                      strictest budget; strongest guarantee.
//
// ── Algebraic shape ─────────────────────────────────────────────────
//
// Carrier: enum class Tolerance over the seven tiers above.
// Order:   RELAXED ⊑ ULP_INT8 ⊑ ULP_FP8 ⊑ ULP_FP16 ⊑ ULP_FP32
//                ⊑ ULP_FP64 ⊑ BITEXACT.
//
// Bottom = RELAXED  (loosest budget — easiest to satisfy; subsumed
//                    by every other tier).
// Top    = BITEXACT (tightest budget — hardest to satisfy; subsumes
//                    every other tier).
// Join   = max      (the tighter of two budgets — what satisfies
//                    BOTH demands).
// Meet   = min      (the looser of two providers — what's satisfied
//                    by either provider).
//
// ── Direction convention (matches Lifetime / Consistency / Conf) ────
//
// Tighter tolerance = higher in the lattice.  `leq(loose, tight)` reads
// "a loose-budget consumer is satisfied by a tight-budget provider"
// — a value computed under BITEXACT subsumes any consumer asking for
// ULP_FP32, which subsumes any asking for ULP_FP16, etc.  This is the
// Crucible-standard subsumption-up direction shared with Lifetime
// and Consistency.
//
// Note that this is REVERSED from the natural numeric ordering of ε:
// smaller ε = tighter budget = higher tier.  Capturing the budget
// semantically (in tier names) rather than as a raw double avoids
// FP-equality hazards in the lattice operations and gives the type
// system finite, enumerable witnesses.
//
// For continuous-ε budgets (the §10 LP allocator's input format),
// use `Tagged<TighterIsBetter, Refined<positive, double>>` or wrap
// `MonotoneLattice<double, std::greater<>>` directly — both compose
// with the Graded substrate.  ToleranceLattice is the discrete
// classification axis BatchPolicy / NumericalRecipe consult; the
// continuous form lives one level up at the LP problem.
//
//   Axiom coverage:
//     TypeSafe — Tolerance is a strong enum (`enum class : uint8_t`);
//                conversion to underlying requires `std::to_underlying`,
//                blocking accidental int math on tolerance tiers.
//     DetSafe — every operation is `constexpr` (NOT `consteval`) so
//                Graded's runtime `pre (L::leq(...))` precondition can
//                fire under the `enforce` contract semantic.
//   Runtime cost:
//     leq / join / meet — single integer compare and a select; the
//     seven-element domain compiles to a 1-byte field with a single
//     branch.  When wrapped at a fixed type-level tier via
//     `ToleranceLattice::At<Tolerance::BITEXACT>` (the conf::Tier
//     pattern), the grade EBO-collapses to zero bytes.
//
// ── At<T> singleton sub-lattice ─────────────────────────────────────
//
// Mirrors ConfLattice::At<Conf>: a per-Tolerance singleton sub-
// lattice with empty element_type, used when an op's tolerance tier
// is fixed at the type level (typical for kernel templates whose
// recipe is pinned at compile time).  `Graded<Absolute,
// ToleranceLattice::At<Tolerance::BITEXACT>, T>` pays zero runtime
// overhead for the grade itself.
//
// See ALGEBRA-14 (#459), ALGEBRA-2 (Lattice.h) for the verifier
// helpers; ALGEBRA-6 (ConfLattice) for the convention this lattice
// mirrors; 25_04_2026.md §10 for the precision-budget calibrator
// use case; MIMIC.md §41 for the cross-vendor numerics CI gate
// that validates per-recipe ULP tolerances.

#include <crucible/algebra/Graded.h>
#include <crucible/algebra/Lattice.h>
#include <crucible/algebra/lattices/ChainLattice.h>

#include <cstdint>
#include <meta>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::algebra::lattices {

// ── Tolerance tier ──────────────────────────────────────────────────
enum class Tolerance : std::uint8_t {
    RELAXED  = 0,    // no error bound (quantized inference)
    ULP_INT8 = 1,    // ~10⁻² (post-training quantization)
    ULP_FP8  = 2,    // ~10⁻³..10⁻² (FP8 tensor cores)
    ULP_FP16 = 3,    // ~10⁻⁴..10⁻³ (FP16/BF16 + FP32 accumulator)
    ULP_FP32 = 4,    // ~10⁻⁷..10⁻⁶ (single-precision ULP)
    ULP_FP64 = 5,    // ~10⁻¹⁵      (double-precision ULP)
    BITEXACT = 6,    // 0           (bit-identical across replicas)
};

// Cardinality + diagnostic name via reflection.
inline constexpr std::size_t tolerance_count =
    std::meta::enumerators_of(^^Tolerance).size();

[[nodiscard]] consteval std::string_view tolerance_name(Tolerance t) noexcept {
    switch (t) {
        case Tolerance::RELAXED:  return "RELAXED";
        case Tolerance::ULP_INT8: return "ULP_INT8";
        case Tolerance::ULP_FP8:  return "ULP_FP8";
        case Tolerance::ULP_FP16: return "ULP_FP16";
        case Tolerance::ULP_FP32: return "ULP_FP32";
        case Tolerance::ULP_FP64: return "ULP_FP64";
        case Tolerance::BITEXACT: return "BITEXACT";
        default:                  return std::string_view{"<unknown Tolerance>"};
    }
}

// ── Full ToleranceLattice (chain order) ─────────────────────────────
//
// Inherits leq/join/meet from ChainLatticeOps<Tolerance> — see
// ChainLattice.h for the rationale (audit Tier-2 dedup).
struct ToleranceLattice : ChainLatticeOps<Tolerance> {
    [[nodiscard]] static constexpr element_type bottom() noexcept {
        return Tolerance::RELAXED;
    }
    [[nodiscard]] static constexpr element_type top() noexcept {
        return Tolerance::BITEXACT;
    }

    [[nodiscard]] static consteval std::string_view name() noexcept {
        return "ToleranceLattice";
    }

    // ── At<T>: singleton sub-lattice at a fixed type-level tier ─────
    //
    // Used by per-op precision wrappers in NumericalRecipe-pinned
    // kernel templates: e.g.
    //   using BitexactKernelOut =
    //       Graded<Absolute, ToleranceLattice::At<BITEXACT>, ...>;
    template <Tolerance T>
    struct At {
        struct element_type {
            using tolerance_value_type = Tolerance;
            [[nodiscard]] constexpr operator tolerance_value_type() const noexcept {
                return T;
            }
            [[nodiscard]] constexpr bool operator==(element_type) const noexcept {
                return true;
            }
        };

        static constexpr Tolerance tier = T;

        [[nodiscard]] static constexpr element_type bottom() noexcept { return {}; }
        [[nodiscard]] static constexpr element_type top()    noexcept { return {}; }
        [[nodiscard]] static constexpr bool         leq(element_type, element_type) noexcept { return true; }
        [[nodiscard]] static constexpr element_type join(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static constexpr element_type meet(element_type, element_type) noexcept { return {}; }

        [[nodiscard]] static consteval std::string_view name() noexcept {
            switch (T) {
                case Tolerance::RELAXED:  return "ToleranceLattice::At<RELAXED>";
                case Tolerance::ULP_INT8: return "ToleranceLattice::At<ULP_INT8>";
                case Tolerance::ULP_FP8:  return "ToleranceLattice::At<ULP_FP8>";
                case Tolerance::ULP_FP16: return "ToleranceLattice::At<ULP_FP16>";
                case Tolerance::ULP_FP32: return "ToleranceLattice::At<ULP_FP32>";
                case Tolerance::ULP_FP64: return "ToleranceLattice::At<ULP_FP64>";
                case Tolerance::BITEXACT: return "ToleranceLattice::At<BITEXACT>";
                default:                  return "ToleranceLattice::At<?>";
            }
        }
    };
};

// ── Convenience aliases ─────────────────────────────────────────────
namespace tolerance {
    using RelaxedTier  = ToleranceLattice::At<Tolerance::RELAXED>;
    using Int8Tier     = ToleranceLattice::At<Tolerance::ULP_INT8>;
    using Fp8Tier      = ToleranceLattice::At<Tolerance::ULP_FP8>;
    using Fp16Tier     = ToleranceLattice::At<Tolerance::ULP_FP16>;
    using Fp32Tier     = ToleranceLattice::At<Tolerance::ULP_FP32>;
    using Fp64Tier     = ToleranceLattice::At<Tolerance::ULP_FP64>;
    using BitexactTier = ToleranceLattice::At<Tolerance::BITEXACT>;
}  // namespace tolerance

// ── Self-test ───────────────────────────────────────────────────────
namespace detail::tolerance_lattice_self_test {

// Cardinality + reflection-based name coverage.
static_assert(tolerance_count == 7,
    "Tolerance catalog diverged from {RELAXED, ULP_INT8, ULP_FP8, "
    "ULP_FP16, ULP_FP32, ULP_FP64, BITEXACT}; confirm intent and "
    "update precision-budget calibrator callers.");

[[nodiscard]] consteval bool every_tolerance_has_name() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^Tolerance));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (tolerance_name([:en:]) ==
            std::string_view{"<unknown Tolerance>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_tolerance_has_name(),
    "tolerance_name() switch missing arm for at least one tier — "
    "add the arm or the new tier leaks the '<unknown Tolerance>' "
    "sentinel into Augur's debug output.");

// Concept conformance — full lattice + each At<T> sub-lattice.
static_assert(Lattice<ToleranceLattice>);
static_assert(BoundedLattice<ToleranceLattice>);
static_assert(Lattice<tolerance::RelaxedTier>);
static_assert(Lattice<tolerance::Int8Tier>);
static_assert(Lattice<tolerance::Fp8Tier>);
static_assert(Lattice<tolerance::Fp16Tier>);
static_assert(Lattice<tolerance::Fp32Tier>);
static_assert(Lattice<tolerance::Fp64Tier>);
static_assert(Lattice<tolerance::BitexactTier>);
static_assert(BoundedLattice<tolerance::BitexactTier>);

// Negative concept assertions — pin ToleranceLattice's character.
static_assert(!UnboundedLattice<ToleranceLattice>);
static_assert(!Semiring<ToleranceLattice>);

// Empty element_type for EBO collapse.
static_assert(std::is_empty_v<tolerance::RelaxedTier::element_type>);
static_assert(std::is_empty_v<tolerance::Int8Tier::element_type>);
static_assert(std::is_empty_v<tolerance::Fp8Tier::element_type>);
static_assert(std::is_empty_v<tolerance::Fp16Tier::element_type>);
static_assert(std::is_empty_v<tolerance::Fp32Tier::element_type>);
static_assert(std::is_empty_v<tolerance::Fp64Tier::element_type>);
static_assert(std::is_empty_v<tolerance::BitexactTier::element_type>);

// EXHAUSTIVE lattice-axiom coverage over (Tolerance)³ = 343 triples.
// Reflection drives the loop so a future tier added to the enum
// auto-extends the coverage at compile time.
[[nodiscard]] consteval bool exhaustive_lattice_check() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^Tolerance));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto ea : enumerators) {
        template for (constexpr auto eb : enumerators) {
            template for (constexpr auto ec : enumerators) {
                if (!verify_bounded_lattice_axioms_at<ToleranceLattice>(
                        [:ea:], [:eb:], [:ec:])) {
                    return false;
                }
            }
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(exhaustive_lattice_check(),
    "ToleranceLattice's chain-order lattice axioms must hold at every "
    "(Tolerance)³ triple — failure indicates a defect in leq/join/meet "
    "or in the underlying enum encoding.");

// Distributive — chain orders are trivially distributive.
[[nodiscard]] consteval bool exhaustive_distributive_check() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^Tolerance));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto ea : enumerators) {
        template for (constexpr auto eb : enumerators) {
            template for (constexpr auto ec : enumerators) {
                if (!verify_distributive_lattice<ToleranceLattice>(
                        [:ea:], [:eb:], [:ec:])) {
                    return false;
                }
            }
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(exhaustive_distributive_check(),
    "ToleranceLattice's chain order must satisfy distributivity at "
    "every (Tolerance)³ triple — a chain order always does, so failure "
    "would indicate a defect in join or meet.");

// Direct order witnesses — the entire chain is increasing.
static_assert( ToleranceLattice::leq(Tolerance::RELAXED,  Tolerance::ULP_INT8));
static_assert( ToleranceLattice::leq(Tolerance::ULP_INT8, Tolerance::ULP_FP8));
static_assert( ToleranceLattice::leq(Tolerance::ULP_FP8,  Tolerance::ULP_FP16));
static_assert( ToleranceLattice::leq(Tolerance::ULP_FP16, Tolerance::ULP_FP32));
static_assert( ToleranceLattice::leq(Tolerance::ULP_FP32, Tolerance::ULP_FP64));
static_assert( ToleranceLattice::leq(Tolerance::ULP_FP64, Tolerance::BITEXACT));
static_assert( ToleranceLattice::leq(Tolerance::RELAXED,  Tolerance::BITEXACT));   // transitive endpoints
static_assert(!ToleranceLattice::leq(Tolerance::BITEXACT, Tolerance::RELAXED));
static_assert(!ToleranceLattice::leq(Tolerance::ULP_FP32, Tolerance::ULP_FP16));

// Pin bottom / top to the chain endpoints.
static_assert(ToleranceLattice::bottom() == Tolerance::RELAXED);
static_assert(ToleranceLattice::top()    == Tolerance::BITEXACT);

// Join tightens budget (max); meet loosens (min).  These are the
// algebraic operations the §10 LP allocator's per-op aggregation
// uses: combining two ops' tolerance contributions takes the
// tighter of the two as the joint requirement.
static_assert(ToleranceLattice::join(Tolerance::RELAXED, Tolerance::BITEXACT) == Tolerance::BITEXACT);
static_assert(ToleranceLattice::join(Tolerance::ULP_FP8, Tolerance::ULP_FP16) == Tolerance::ULP_FP16);
static_assert(ToleranceLattice::meet(Tolerance::RELAXED, Tolerance::BITEXACT) == Tolerance::RELAXED);
static_assert(ToleranceLattice::meet(Tolerance::ULP_FP32, Tolerance::ULP_FP16) == Tolerance::ULP_FP16);

// Diagnostic names.
static_assert(ToleranceLattice::name() == "ToleranceLattice");
static_assert(tolerance::RelaxedTier::name()  == "ToleranceLattice::At<RELAXED>");
static_assert(tolerance::Int8Tier::name()     == "ToleranceLattice::At<ULP_INT8>");
static_assert(tolerance::Fp8Tier::name()      == "ToleranceLattice::At<ULP_FP8>");
static_assert(tolerance::Fp16Tier::name()     == "ToleranceLattice::At<ULP_FP16>");
static_assert(tolerance::Fp32Tier::name()     == "ToleranceLattice::At<ULP_FP32>");
static_assert(tolerance::Fp64Tier::name()     == "ToleranceLattice::At<ULP_FP64>");
static_assert(tolerance::BitexactTier::name() == "ToleranceLattice::At<BITEXACT>");

// Reflection-driven coverage check on At<T>::name() — same discipline
// as ConfLattice's every_at_conf_has_name.
[[nodiscard]] consteval bool every_at_tolerance_has_name() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^Tolerance));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (ToleranceLattice::At<([:en:])>::name() ==
            std::string_view{"ToleranceLattice::At<?>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_at_tolerance_has_name(),
    "ToleranceLattice::At<T>::name() switch missing an arm for at "
    "least one tier — add the arm or the new tier leaks the "
    "'ToleranceLattice::At<?>' sentinel.");

// Convenience aliases resolve correctly.
static_assert(tolerance::RelaxedTier::tier  == Tolerance::RELAXED);
static_assert(tolerance::Int8Tier::tier     == Tolerance::ULP_INT8);
static_assert(tolerance::Fp8Tier::tier      == Tolerance::ULP_FP8);
static_assert(tolerance::Fp16Tier::tier     == Tolerance::ULP_FP16);
static_assert(tolerance::Fp32Tier::tier     == Tolerance::ULP_FP32);
static_assert(tolerance::Fp64Tier::tier     == Tolerance::ULP_FP64);
static_assert(tolerance::BitexactTier::tier == Tolerance::BITEXACT);

// ── Layout invariants on Graded<...,At<T>,T_> ───────────────────────
struct OneByteValue   { char c{0}; };
struct EightByteValue { unsigned long long v{0}; };

// BitexactTier — the most semantically-loaded tier (BITEXACT_STRICT
// recipe gate, federated-replay invariant).  Witnessed against
// arithmetic T to pin parity across the trivially-default-constructible
// axis (audit-foundation drop).
template <typename T_>
using BitexactGraded = Graded<ModalityKind::Absolute, tolerance::BitexactTier, T_>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(BitexactGraded, OneByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(BitexactGraded, EightByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(BitexactGraded, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(BitexactGraded, double);

// Mid-tier (FP16) — typical for mixed-precision training intermediates.
template <typename T_>
using Fp16Graded = Graded<ModalityKind::Absolute, tolerance::Fp16Tier, T_>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(Fp16Graded, EightByteValue);

// RelaxedTier — quantized inference / exploration paths.  EBO must
// collapse identically across all tiers.
template <typename T_>
using RelaxedGraded = Graded<ModalityKind::Absolute, tolerance::RelaxedTier, T_>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(RelaxedGraded, EightByteValue);

// ── Runtime smoke test ─────────────────────────────────────────────
//
// Per feedback_algebra_runtime_smoke_test_discipline memory: exercise
// lattice ops AND Graded::weaken / compose with non-constant arguments
// at runtime.
inline void runtime_smoke_test() {
    // Full ToleranceLattice ops at runtime.
    Tolerance a = Tolerance::RELAXED;
    Tolerance b = Tolerance::BITEXACT;
    [[maybe_unused]] bool      l1   = ToleranceLattice::leq(a, b);
    [[maybe_unused]] Tolerance j1   = ToleranceLattice::join(a, b);
    [[maybe_unused]] Tolerance m1   = ToleranceLattice::meet(a, b);
    [[maybe_unused]] Tolerance bot  = ToleranceLattice::bottom();
    [[maybe_unused]] Tolerance top  = ToleranceLattice::top();

    // Mid-tier ops — chains through the FP* portion of the lattice.
    Tolerance fp16 = Tolerance::ULP_FP16;
    Tolerance fp32 = Tolerance::ULP_FP32;
    [[maybe_unused]] Tolerance j2 = ToleranceLattice::join(fp16, fp32);    // ULP_FP32
    [[maybe_unused]] Tolerance m2 = ToleranceLattice::meet(fp16, fp32);    // ULP_FP16

    // Graded<Absolute, BitexactTier, T> at runtime.
    OneByteValue v{42};
    BitexactGraded<OneByteValue> initial{v, tolerance::BitexactTier::bottom()};
    auto widened   = initial.weaken(tolerance::BitexactTier::top());
    auto composed  = initial.compose(widened);
    auto rv_widen  = std::move(widened).weaken(tolerance::BitexactTier::top());

    [[maybe_unused]] auto g  = rv_widen.grade();
    [[maybe_unused]] auto vc = composed.peek().c;

    // Conversion: At<Tolerance>::element_type → Tolerance at runtime.
    tolerance::BitexactTier::element_type e{};
    [[maybe_unused]] Tolerance rec = e;
}

}  // namespace detail::tolerance_lattice_self_test

}  // namespace crucible::algebra::lattices
