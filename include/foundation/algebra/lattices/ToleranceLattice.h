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

#include <foundation/algebra/ClaimOrientation.h>
#include <foundation/algebra/Lattice.h>
#include <foundation/algebra/lattices/ChainLattice.h>
#include <foundation/reflect/EnumName.h>

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

// A tighter error bound is the stronger claim.
struct ToleranceLattice : EnumChainLattice<ToleranceLattice, Tolerance, ClaimOrientation::stronger_is_higher> {
    template <Tolerance T>
    struct At : PinnedAt<ToleranceLattice, T> {
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

static_assert(::foundation::reflect::enum_count<Tolerance> == 7,
              "Tolerance catalog diverged from {RELAXED, ULP_INT8, ULP_FP8, ULP_FP16, ULP_FP32, ULP_FP64, "
              "BITEXACT}.  Confirm intent and update the precision-budget callers.");

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

static_assert(tolerance::RelaxedTier::tier == Tolerance::RELAXED);
static_assert(tolerance::BitexactTier::tier == Tolerance::BITEXACT);

}  // namespace detail::tolerance_lattice_self_test

}  // namespace foundation::algebra::lattices
