#pragma once

// The partial order of the memory-fence strength a code region provides.
// bottom is None, top is FullFence.  A stronger fence satisfies a weaker
// requirement, so leq(required, provided) is the admission direction and
// join is the strictest-wins composition.
//
// The domain brackets the standard memory-order tags on both ends.
// CompilerBarrier sits below them because it constrains only the
// optimizer and emits no instruction.  FullFence sits above them because
// a standalone fence instruction orders every surrounding memory
// operation, not just the tagged one.
//
// AcquireLoad and ReleaseStore are incomparable, as they are in the C++
// memory model ([atomics.order]): an acquire load orders the operations
// after it, a release store orders the operations before it, and neither
// gives the guarantee of the other.  So the order is not a chain.  It is
// a chain with one diamond:
//
//   None < CompilerBarrier < {AcquireLoad, ReleaseStore} < AcqRel < SeqCst < FullFence
//
// AcqRel is the join of the two, because acq_rel is both an acquire and a
// release, and CompilerBarrier is their meet.  Reading the two as a chain
// would let a release store satisfy an acquire requirement, which orders
// the wrong side of the operation.  The order is a distributive lattice,
// and the self-test below proves the laws at every triple.
//
// A fence-then-relaxed pattern whose correctness depends on a particular
// architecture is claimed separately.  Nothing here proves it.

#include <foundation/algebra/Graded.h>
#include <foundation/algebra/Lattice.h>
#include <foundation/algebra/lattices/ChainLattice.h>
#include <foundation/reflect/Enumerate.h>

#include <cstddef>
#include <cstdint>
#include <meta>
#include <string_view>
#include <type_traits>
#include <utility>

namespace foundation::algebra::lattices {

enum class BarrierStrength : std::uint8_t {
    None = 0,  // no barrier at all
    CompilerBarrier = 1,  // asm volatile("":::"memory") — optimizer only, no instruction
    AcquireLoad = 2,  // acquire ordering, prior loads fenced
    ReleaseStore = 3,  // release ordering, prior-store visibility
    AcqRel = 4,  // combined acquire and release
    SeqCst = 5,  // sequentially consistent, one total order
    FullFence = 6,  // standalone mfence or DMB ISH
};

// The identifier of k, or "<unknown BarrierStrength>" for a value
// outside the enum.
[[nodiscard]] consteval std::string_view barrier_strength_name(BarrierStrength k) noexcept {
    return ::foundation::reflect::enum_name(k);
}

// The height of a strength in the order: its underlying value, with the
// two incomparable tags at one height.  The enumerator values are pinned
// in EnumValuePins.h, so the height follows the declaration.  A value
// outside the enum gets a height above FullFence, and every operation
// below stays defined for it.
[[nodiscard]] constexpr std::uint8_t barrier_strength_height(BarrierStrength k) noexcept {
    const std::uint8_t value = std::to_underlying(k);
    return value <= std::to_underlying(BarrierStrength::AcquireLoad) ? value : static_cast<std::uint8_t>(value - 1);
}

struct BarrierStrengthLattice {
    using element_type = BarrierStrength;

    // A stronger fence is the stronger claim.
    static constexpr ClaimOrientation claim_orientation = ClaimOrientation::stronger_is_higher;

    [[nodiscard]] static constexpr BarrierStrength bottom() noexcept { return BarrierStrength::None; }
    [[nodiscard]] static constexpr BarrierStrength top() noexcept { return BarrierStrength::FullFence; }
    [[nodiscard]] static consteval std::string_view name() noexcept { return "BarrierStrengthLattice"; }

    // Two distinct strengths at one height are incomparable.  Every other
    // pair is ordered by height.
    [[nodiscard]] static constexpr bool leq(BarrierStrength a, BarrierStrength b) noexcept {
        if (a == b) return true;
        return barrier_strength_height(a) < barrier_strength_height(b);
    }

    // The only two distinct strengths at one height are AcquireLoad and
    // ReleaseStore, and the self-test below proves that by reflection.
    // Their join is AcqRel and their meet is CompilerBarrier.
    [[nodiscard]] static constexpr BarrierStrength join(BarrierStrength a, BarrierStrength b) noexcept {
        if (a == b) return a;
        if (barrier_strength_height(a) == barrier_strength_height(b)) return BarrierStrength::AcqRel;
        return barrier_strength_height(a) > barrier_strength_height(b) ? a : b;
    }

    [[nodiscard]] static constexpr BarrierStrength meet(BarrierStrength a, BarrierStrength b) noexcept {
        if (a == b) return a;
        if (barrier_strength_height(a) == barrier_strength_height(b)) return BarrierStrength::CompilerBarrier;
        return barrier_strength_height(a) < barrier_strength_height(b) ? a : b;
    }

    template <BarrierStrength K>
    struct AtElement : PinnedElement<K> {
        using barrier_strength_value_type = BarrierStrength;
    };

    template <BarrierStrength K>
    struct At : PinnedAt<BarrierStrengthLattice, K, AtElement<K>> {
        static constexpr BarrierStrength tier = K;
    };
};

namespace detail::barrier_strength_lattice_self_test {

inline constexpr std::size_t barrier_strength_count = ::foundation::reflect::enum_count<BarrierStrength>;

static_assert(barrier_strength_count == 7, "BarrierStrength diverged from {None, CompilerBarrier, AcquireLoad, "
                                           "ReleaseStore, AcqRel, SeqCst, FullFence}.  A new tier appends at "
                                           "the next free ordinal.");

static_assert(std::to_underlying(BarrierStrength::None) == 0);

static_assert(std::to_underlying(BarrierStrength::FullFence) == 6);

static_assert(std::is_same_v<std::underlying_type_t<BarrierStrength>, std::uint8_t>);

static_assert(Lattice<BarrierStrengthLattice>);
static_assert(BoundedLattice<BarrierStrengthLattice>);

// The lattice laws at every triple of the seven strengths, and leq, join
// and meet in agreement at every pair: reflexive, antisymmetric and
// transitive, and join and meet the least upper and greatest lower
// bounds.  The walk reads the enumerators by reflection.
static_assert(verify_enum_lattice_exhaustive<BarrierStrengthLattice>(),
              "BarrierStrengthLattice: a partial-order or bound law fails at some triple of strengths.");

// A chain with one diamond is distributive.  The law holds at every
// triple, so a later strength that breaks it stops the build here.
static_assert(verify_chain_lattice_distributive_exhaustive<BarrierStrengthLattice>(),
              "BarrierStrengthLattice: the distributive law fails at some triple of strengths.");

static_assert(verify_pinned_at<BarrierStrengthLattice>(),
              "BarrierStrengthLattice::At<K>: a pinned grade lost its emptiness, its conversion back to K, or its "
              "reflected name.");

// The incomparable pairs, derived from the order rather than listed.  The
// only pair is {AcquireLoad, ReleaseStore}, which join and meet above
// depend on.  A new strength at an existing height adds a pair, and this
// assertion names the fault before join and meet can give a wrong bound.
[[nodiscard]] consteval std::size_t incomparable_pairs() noexcept {
    std::size_t pairs = 0;
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^BarrierStrength));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto first : enumerators) {
        template for (constexpr auto second : enumerators) {
            constexpr BarrierStrength a = [:first:];
            constexpr BarrierStrength b = [:second:];
            if (std::to_underlying(a) < std::to_underlying(b) && !BarrierStrengthLattice::leq(a, b)
                && !BarrierStrengthLattice::leq(b, a)) {
                ++pairs;
            }
        }
    }
#pragma GCC diagnostic pop
    return pairs;
}
static_assert(incomparable_pairs() == 1,
              "BarrierStrengthLattice: the order must have exactly one incomparable pair, AcquireLoad and "
              "ReleaseStore.  join and meet name AcqRel and CompilerBarrier as its bounds.");
static_assert(!BarrierStrengthLattice::leq(BarrierStrength::AcquireLoad, BarrierStrength::ReleaseStore)
                  && !BarrierStrengthLattice::leq(BarrierStrength::ReleaseStore, BarrierStrength::AcquireLoad),
              "An acquire load orders the operations after it and a release store the operations before it.  "
              "Neither satisfies a requirement for the other.");
static_assert(BarrierStrengthLattice::join(BarrierStrength::AcquireLoad, BarrierStrength::ReleaseStore)
                  == BarrierStrength::AcqRel,
              "acq_rel is both an acquire and a release, so it is the least strength that satisfies both.");
static_assert(BarrierStrengthLattice::meet(BarrierStrength::AcquireLoad, BarrierStrength::ReleaseStore)
                  == BarrierStrength::CompilerBarrier);

static_assert(!::foundation::algebra::Semiring<BarrierStrengthLattice>);

static_assert(BarrierStrengthLattice::bottom() == BarrierStrength::None);
static_assert(BarrierStrengthLattice::top() == BarrierStrength::FullFence);

static_assert(BarrierStrengthLattice::name() == std::string_view{"BarrierStrengthLattice"});

static_assert(BarrierStrengthLattice::leq(BarrierStrength::AcqRel, BarrierStrength::SeqCst),
              "A SeqCst fence satisfies an AcqRel requirement.  leq(required, "
              "provided) is the admission direction.");
static_assert(!BarrierStrengthLattice::leq(BarrierStrength::SeqCst, BarrierStrength::AcqRel),
              "An AcqRel fence does not satisfy a SeqCst requirement.");

static_assert(BarrierStrengthLattice::join(BarrierStrength::None, BarrierStrength::FullFence)
                  == BarrierStrength::FullFence,
              "join gives the strictest-wins reading on this chain, because the "
              "top is FullFence.  join(None, FullFence) returns FullFence, the "
              "stronger of the two.");
static_assert(BarrierStrengthLattice::meet(BarrierStrength::None, BarrierStrength::FullFence) == BarrierStrength::None,
              "meet gives the weakest floor, because the bottom is None.  An "
              "admission gate that grants only what every participant provides "
              "calls meet.");

static_assert(BarrierStrengthLattice::At<BarrierStrength::SeqCst>::tier == BarrierStrength::SeqCst);
static_assert(BarrierStrengthLattice::At<BarrierStrength::SeqCst>::name() == "BarrierStrengthLattice::At<SeqCst>");
static_assert(BarrierStrengthLattice::At<static_cast<BarrierStrength>(255)>::name() == "BarrierStrengthLattice::At<?>");

static_assert(barrier_strength_name(BarrierStrength::CompilerBarrier) == "CompilerBarrier");
static_assert(barrier_strength_name(static_cast<BarrierStrength>(255)) == "<unknown BarrierStrength>");

struct OneByteValue {
    char c{0};
};
struct EightByteValue {
    unsigned long long v{0};
};

template <typename T_>
using NoneGraded = Graded<ModalityKind::Absolute, BarrierStrengthLattice::At<BarrierStrength::None>, T_>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(NoneGraded, OneByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(NoneGraded, EightByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(NoneGraded, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(NoneGraded, double);

template <typename T_>
using FullFenceGraded = Graded<ModalityKind::Absolute, BarrierStrengthLattice::At<BarrierStrength::FullFence>, T_>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(FullFenceGraded, EightByteValue);

}  // namespace detail::barrier_strength_lattice_self_test

}  // namespace foundation::algebra::lattices
