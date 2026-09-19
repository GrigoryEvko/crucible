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

#include <foundation/algebra/Graded.h>
#include <foundation/algebra/Lattice.h>
#include <foundation/algebra/lattices/ChainLattice.h>
#include <foundation/reflect/Enumerate.h>

#include <cstdint>
#include <meta>
#include <string_view>
#include <type_traits>
#include <utility>

namespace foundation::algebra::lattices {

enum class Tolerance : std::uint8_t {
    RELAXED = 0,  // no error bound
    ULP_INT8 = 1,  // ~10⁻² (post-training quantization)
    ULP_FP8 = 2,  // ~10⁻³..10⁻² (FP8 tensor cores)
    ULP_FP16 = 3,  // ~10⁻⁴..10⁻³ (FP16/BF16 with an FP32 accumulator)
    ULP_FP32 = 4,  // ~10⁻⁷..10⁻⁶ (single-precision ULP)
    ULP_FP64 = 5,  // ~10⁻¹⁵      (double-precision ULP)
    BITEXACT = 6,  // 0           (bit-identical across replicas and replays)
};

inline constexpr std::size_t tolerance_count = ::foundation::reflect::enum_count<Tolerance>;

// The identifier of t, or "<unknown Tolerance>" for a value outside the
// enum.
[[nodiscard]] consteval std::string_view tolerance_name(Tolerance t) noexcept {
    return ::foundation::reflect::enum_name(t);
}

struct ToleranceLattice : ChainLatticeOps<Tolerance> {
    [[nodiscard]] static constexpr element_type bottom() noexcept { return Tolerance::RELAXED; }
    [[nodiscard]] static constexpr element_type top() noexcept { return Tolerance::BITEXACT; }

    [[nodiscard]] static consteval std::string_view name() noexcept { return "ToleranceLattice"; }

    template <Tolerance T>
    struct AtElement : PinnedElement<T> {
        using tolerance_value_type = Tolerance;
    };

    template <Tolerance T>
    struct At : PinnedAt<ToleranceLattice, T, AtElement<T>> {
        static constexpr Tolerance tier = T;
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

static_assert(verify_chain_lattice<ToleranceLattice>(), "ToleranceLattice: the chain order, the pinned grades or the "
                                                        "reflected names diverged from the Tolerance enumerator list.");

static_assert(!UnboundedLattice<ToleranceLattice>);
static_assert(!Semiring<ToleranceLattice>);

static_assert(ToleranceLattice::bottom() == Tolerance::RELAXED);
static_assert(ToleranceLattice::top() == Tolerance::BITEXACT);

static_assert(ToleranceLattice::join(Tolerance::RELAXED, Tolerance::BITEXACT) == Tolerance::BITEXACT,
              "join gives the strictest-wins reading on this chain, because the "
              "top is BITEXACT.  join(RELAXED, BITEXACT) returns BITEXACT, the "
              "tighter budget.");
static_assert(ToleranceLattice::meet(Tolerance::RELAXED, Tolerance::BITEXACT) == Tolerance::RELAXED,
              "meet gives the loosest floor, because the bottom is RELAXED.  A "
              "gate that admits any tolerance calls meet.");

static_assert(ToleranceLattice::name() == "ToleranceLattice");
static_assert(tolerance::RelaxedTier::name() == "ToleranceLattice::At<RELAXED>");
static_assert(tolerance::BitexactTier::name() == "ToleranceLattice::At<BITEXACT>");
static_assert(ToleranceLattice::At<static_cast<Tolerance>(255)>::name() == "ToleranceLattice::At<?>");

static_assert(tolerance_name(Tolerance::ULP_FP16) == "ULP_FP16");
static_assert(tolerance_name(static_cast<Tolerance>(255)) == "<unknown Tolerance>");

static_assert(tolerance::RelaxedTier::tier == Tolerance::RELAXED);
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

}  // namespace foundation::algebra::lattices
