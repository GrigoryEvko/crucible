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
// and the self-test in the check file of this header proves the laws at
// every triple.
//
// A fence-then-relaxed pattern whose correctness depends on a particular
// architecture is claimed separately.  Nothing here proves it.

#include <foundation/algebra/ClaimOrientation.h>
#include <foundation/algebra/Lattice.h>
#include <foundation/algebra/lattices/ChainLattice.h>
#include <foundation/reflect/EnumName.h>

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
    // ReleaseStore, and the self-test in the check file proves that by
    // reflection.
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
    struct At : PinnedAt<BarrierStrengthLattice, K> {
        static constexpr BarrierStrength tier = K;
    };
};

}  // namespace foundation::algebra::lattices
