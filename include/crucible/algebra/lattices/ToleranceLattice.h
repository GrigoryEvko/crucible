#pragma once

// Chain over numeric-tolerance budgets.  bottom is RELAXED, the loosest
// budget, and top is BITEXACT, which admits no error at all.  Tighter is
// higher, so leq(loose, tight) reads "a loose consumer is satisfied by a
// tight provider".  join takes the tighter of two budgets, which is the
// joint requirement, and meet takes the looser.
//
// The order runs opposite to the numeric ordering of the error bound: a
// smaller bound sits higher.  The tiers are named rather than carried as
// a raw bound, so the lattice operations never compare floating-point
// values and the witness set stays finite and enumerable.  A continuous
// budget belongs one level up, in whatever solves for it.

#include <crucible/algebra/Graded.h>
#include <crucible/algebra/Lattice.h>
#include <crucible/algebra/lattices/ChainLattice.h>

#include <cstdint>
#include <meta>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::algebra::lattices {

enum class Tolerance : std::uint8_t {
    RELAXED = 0,  // no error bound
    ULP_INT8 = 1,  // ~10⁻² (post-training quantization)
    ULP_FP8 = 2,  // ~10⁻³..10⁻² (FP8 tensor cores)
    ULP_FP16 = 3,  // ~10⁻⁴..10⁻³ (FP16/BF16 with an FP32 accumulator)
    ULP_FP32 = 4,  // ~10⁻⁷..10⁻⁶ (single-precision ULP)
    ULP_FP64 = 5,  // ~10⁻¹⁵      (double-precision ULP)
    BITEXACT = 6,  // 0           (bit-identical across replicas and replays)
};

inline constexpr std::size_t tolerance_count = std::meta::enumerators_of(^^Tolerance).size();

[[nodiscard]] consteval std::string_view tolerance_name(Tolerance t) noexcept {
    switch (t) {
        case Tolerance::RELAXED:
            return "RELAXED";
        case Tolerance::ULP_INT8:
            return "ULP_INT8";
        case Tolerance::ULP_FP8:
            return "ULP_FP8";
        case Tolerance::ULP_FP16:
            return "ULP_FP16";
        case Tolerance::ULP_FP32:
            return "ULP_FP32";
        case Tolerance::ULP_FP64:
            return "ULP_FP64";
        case Tolerance::BITEXACT:
            return "BITEXACT";
        default:
            return std::string_view{"<unknown Tolerance>"};
    }
}

struct ToleranceLattice : ChainLatticeOps<Tolerance> {
    [[nodiscard]] static constexpr element_type bottom() noexcept { return Tolerance::RELAXED; }
    [[nodiscard]] static constexpr element_type top() noexcept { return Tolerance::BITEXACT; }

    [[nodiscard]] static consteval std::string_view name() noexcept { return "ToleranceLattice"; }

    template <Tolerance T>
    struct At {
        struct element_type {
            using tolerance_value_type = Tolerance;
            [[nodiscard]] constexpr operator tolerance_value_type() const noexcept { return T; }
            [[nodiscard]] constexpr bool operator==(element_type) const noexcept { return true; }
        };

        static constexpr Tolerance tier = T;

        [[nodiscard]] static constexpr element_type bottom() noexcept { return {}; }
        [[nodiscard]] static constexpr element_type top() noexcept { return {}; }
        [[nodiscard]] static constexpr bool leq(element_type, element_type) noexcept { return true; }
        [[nodiscard]] static constexpr element_type join(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static constexpr element_type meet(element_type, element_type) noexcept { return {}; }

        [[nodiscard]] static consteval std::string_view name() noexcept {
            switch (T) {
                case Tolerance::RELAXED:
                    return "ToleranceLattice::At<RELAXED>";
                case Tolerance::ULP_INT8:
                    return "ToleranceLattice::At<ULP_INT8>";
                case Tolerance::ULP_FP8:
                    return "ToleranceLattice::At<ULP_FP8>";
                case Tolerance::ULP_FP16:
                    return "ToleranceLattice::At<ULP_FP16>";
                case Tolerance::ULP_FP32:
                    return "ToleranceLattice::At<ULP_FP32>";
                case Tolerance::ULP_FP64:
                    return "ToleranceLattice::At<ULP_FP64>";
                case Tolerance::BITEXACT:
                    return "ToleranceLattice::At<BITEXACT>";
                default:
                    return "ToleranceLattice::At<?>";
            }
        }
    };
};

namespace tolerance {
using RelaxedTier = ToleranceLattice::At<Tolerance::RELAXED>;
using Int8Tier = ToleranceLattice::At<Tolerance::ULP_INT8>;
using Fp8Tier = ToleranceLattice::At<Tolerance::ULP_FP8>;
using Fp16Tier = ToleranceLattice::At<Tolerance::ULP_FP16>;
using Fp32Tier = ToleranceLattice::At<Tolerance::ULP_FP32>;
using Fp64Tier = ToleranceLattice::At<Tolerance::ULP_FP64>;
using BitexactTier = ToleranceLattice::At<Tolerance::BITEXACT>;
}  // namespace tolerance

namespace detail::tolerance_lattice_self_test {

static_assert(tolerance_count == 7, "Tolerance catalog diverged from {RELAXED, ULP_INT8, ULP_FP8, "
                                    "ULP_FP16, ULP_FP32, ULP_FP64, BITEXACT}.  Confirm intent and "
                                    "update the precision-budget callers.");

[[nodiscard]] consteval bool every_tolerance_has_name() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^Tolerance));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (tolerance_name([:en:]) == std::string_view{"<unknown Tolerance>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_tolerance_has_name(), "tolerance_name() switch missing an arm for at least one tier.  "
                                          "Add the arm or the new tier leaks the '<unknown Tolerance>' "
                                          "sentinel into diagnostic output.");

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

static_assert(!UnboundedLattice<ToleranceLattice>);
static_assert(!Semiring<ToleranceLattice>);

static_assert(std::is_empty_v<tolerance::RelaxedTier::element_type>);
static_assert(std::is_empty_v<tolerance::Int8Tier::element_type>);
static_assert(std::is_empty_v<tolerance::Fp8Tier::element_type>);
static_assert(std::is_empty_v<tolerance::Fp16Tier::element_type>);
static_assert(std::is_empty_v<tolerance::Fp32Tier::element_type>);
static_assert(std::is_empty_v<tolerance::Fp64Tier::element_type>);
static_assert(std::is_empty_v<tolerance::BitexactTier::element_type>);

static_assert(verify_chain_lattice_exhaustive<ToleranceLattice>(),
              "ToleranceLattice chain-order lattice axioms fail at some triple.  "
              "The defect is in leq, join, meet or the enum encoding.");
static_assert(verify_chain_lattice_distributive_exhaustive<ToleranceLattice>(),
              "ToleranceLattice chain fails distributivity at some triple.  A "
              "chain order always satisfies it, so the defect is in join or "
              "meet.");

static_assert(ToleranceLattice::leq(Tolerance::RELAXED, Tolerance::ULP_INT8));
static_assert(ToleranceLattice::leq(Tolerance::ULP_INT8, Tolerance::ULP_FP8));
static_assert(ToleranceLattice::leq(Tolerance::ULP_FP8, Tolerance::ULP_FP16));
static_assert(ToleranceLattice::leq(Tolerance::ULP_FP16, Tolerance::ULP_FP32));
static_assert(ToleranceLattice::leq(Tolerance::ULP_FP32, Tolerance::ULP_FP64));
static_assert(ToleranceLattice::leq(Tolerance::ULP_FP64, Tolerance::BITEXACT));
static_assert(ToleranceLattice::leq(Tolerance::RELAXED, Tolerance::BITEXACT));
static_assert(!ToleranceLattice::leq(Tolerance::BITEXACT, Tolerance::RELAXED));
static_assert(!ToleranceLattice::leq(Tolerance::ULP_FP32, Tolerance::ULP_FP16));

static_assert(ToleranceLattice::bottom() == Tolerance::RELAXED);
static_assert(ToleranceLattice::top() == Tolerance::BITEXACT);

static_assert(ToleranceLattice::join(Tolerance::RELAXED, Tolerance::BITEXACT) == Tolerance::BITEXACT);
static_assert(ToleranceLattice::join(Tolerance::ULP_FP8, Tolerance::ULP_FP16) == Tolerance::ULP_FP16);
static_assert(ToleranceLattice::meet(Tolerance::RELAXED, Tolerance::BITEXACT) == Tolerance::RELAXED);
static_assert(ToleranceLattice::meet(Tolerance::ULP_FP32, Tolerance::ULP_FP16) == Tolerance::ULP_FP16);

static_assert(ToleranceLattice::join(Tolerance::RELAXED, Tolerance::BITEXACT) == Tolerance::BITEXACT,
              "join gives the strictest-wins reading on this chain, because the "
              "top is BITEXACT.  join(RELAXED, BITEXACT) returns BITEXACT, the "
              "tighter budget.");
static_assert(ToleranceLattice::meet(Tolerance::RELAXED, Tolerance::BITEXACT) == Tolerance::RELAXED,
              "meet gives the loosest floor, because the bottom is RELAXED.  A "
              "gate that admits any tolerance calls meet.");

static_assert(ToleranceLattice::name() == "ToleranceLattice");
static_assert(tolerance::RelaxedTier::name() == "ToleranceLattice::At<RELAXED>");
static_assert(tolerance::Int8Tier::name() == "ToleranceLattice::At<ULP_INT8>");
static_assert(tolerance::Fp8Tier::name() == "ToleranceLattice::At<ULP_FP8>");
static_assert(tolerance::Fp16Tier::name() == "ToleranceLattice::At<ULP_FP16>");
static_assert(tolerance::Fp32Tier::name() == "ToleranceLattice::At<ULP_FP32>");
static_assert(tolerance::Fp64Tier::name() == "ToleranceLattice::At<ULP_FP64>");
static_assert(tolerance::BitexactTier::name() == "ToleranceLattice::At<BITEXACT>");

[[nodiscard]] consteval bool every_at_tolerance_has_name() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^Tolerance));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (ToleranceLattice::At<([:en:])>::name() == std::string_view{"ToleranceLattice::At<?>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_at_tolerance_has_name(), "ToleranceLattice::At<T>::name() switch missing an arm for at "
                                             "least one tier.  Add the arm or the new tier leaks the "
                                             "'ToleranceLattice::At<?>' sentinel.");

static_assert(tolerance::RelaxedTier::tier == Tolerance::RELAXED);
static_assert(tolerance::Int8Tier::tier == Tolerance::ULP_INT8);
static_assert(tolerance::Fp8Tier::tier == Tolerance::ULP_FP8);
static_assert(tolerance::Fp16Tier::tier == Tolerance::ULP_FP16);
static_assert(tolerance::Fp32Tier::tier == Tolerance::ULP_FP32);
static_assert(tolerance::Fp64Tier::tier == Tolerance::ULP_FP64);
static_assert(tolerance::BitexactTier::tier == Tolerance::BITEXACT);

struct OneByteValue {
    char c{0};
};
struct EightByteValue {
    unsigned long long v{0};
};

template <typename T_>
using BitexactGraded = Graded<ModalityKind::Absolute, tolerance::BitexactTier, T_>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(BitexactGraded, OneByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(BitexactGraded, EightByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(BitexactGraded, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(BitexactGraded, double);

template <typename T_>
using Fp16Graded = Graded<ModalityKind::Absolute, tolerance::Fp16Tier, T_>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(Fp16Graded, EightByteValue);

template <typename T_>
using RelaxedGraded = Graded<ModalityKind::Absolute, tolerance::RelaxedTier, T_>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(RelaxedGraded, EightByteValue);

// Static assertions alone can mask consteval, SFINAE and inline-body
// defects.  These calls pass non-constant arguments.
inline void runtime_smoke_test() {
    Tolerance a = Tolerance::RELAXED;
    Tolerance b = Tolerance::BITEXACT;
    [[maybe_unused]] bool l1 = ToleranceLattice::leq(a, b);
    [[maybe_unused]] Tolerance j1 = ToleranceLattice::join(a, b);
    [[maybe_unused]] Tolerance m1 = ToleranceLattice::meet(a, b);
    [[maybe_unused]] Tolerance bot = ToleranceLattice::bottom();
    [[maybe_unused]] Tolerance top = ToleranceLattice::top();

    Tolerance fp16 = Tolerance::ULP_FP16;
    Tolerance fp32 = Tolerance::ULP_FP32;
    [[maybe_unused]] Tolerance j2 = ToleranceLattice::join(fp16, fp32);
    [[maybe_unused]] Tolerance m2 = ToleranceLattice::meet(fp16, fp32);

    OneByteValue v{42};
    BitexactGraded<OneByteValue> initial{v, tolerance::BitexactTier::bottom()};
    auto widened = initial.weaken(tolerance::BitexactTier::top());
    auto composed = initial.compose(widened);
    auto rv_widen = std::move(widened).weaken(tolerance::BitexactTier::top());

    [[maybe_unused]] auto g = rv_widen.grade();
    [[maybe_unused]] auto vc = composed.peek().c;

    tolerance::BitexactTier::element_type e{};
    [[maybe_unused]] Tolerance rec = e;
}

}  // namespace detail::tolerance_lattice_self_test

}  // namespace crucible::algebra::lattices
