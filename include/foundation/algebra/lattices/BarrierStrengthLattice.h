#pragma once

// Chain over the memory-fence strength a code region provides.  bottom
// is None, top is FullFence.  A stronger fence satisfies a weaker
// requirement, so leq(required, provided) is the admission direction and
// join is the strictest-wins composition.
//
// The domain brackets the standard memory-order tags on both ends.
// CompilerBarrier sits below them because it constrains only the
// optimizer and emits no instruction.  FullFence sits above them because
// a standalone fence instruction orders every surrounding memory
// operation, not just the tagged one.
//
// Acquire and release are incomparable in the C++ memory model: neither
// provides the other's guarantee.  This chain linearizes them into a
// strength ladder because gating asks only whether a region provides at
// least the required strength, and a stronger rung is always a safe
// over-approximation.  A fence-then-relaxed pattern whose correctness
// depends on a particular architecture is claimed separately.  Nothing
// here proves it.

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

struct BarrierStrengthLattice : ChainLatticeOps<BarrierStrength> {
    [[nodiscard]] static constexpr BarrierStrength bottom() noexcept { return BarrierStrength::None; }
    [[nodiscard]] static constexpr BarrierStrength top() noexcept { return BarrierStrength::FullFence; }
    [[nodiscard]] static consteval std::string_view name() noexcept { return "BarrierStrengthLattice"; }

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

static_assert(verify_chain_lattice<BarrierStrengthLattice>(),
              "BarrierStrengthLattice: the chain order, the pinned grades or the "
              "reflected names diverged from the BarrierStrength enumerator list.");

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

// Static assertions alone can mask consteval, SFINAE and inline-body
// defects.  These calls pass non-constant arguments.
inline void runtime_smoke_test() {
    BarrierStrength a = BarrierStrength::None;
    BarrierStrength b = BarrierStrength::FullFence;
    [[maybe_unused]] bool rl = BarrierStrengthLattice::leq(a, b);
    [[maybe_unused]] BarrierStrength rj = BarrierStrengthLattice::join(a, b);
    [[maybe_unused]] BarrierStrength rm = BarrierStrengthLattice::meet(a, b);
    [[maybe_unused]] BarrierStrength bot = BarrierStrengthLattice::bottom();
    [[maybe_unused]] BarrierStrength topv = BarrierStrengthLattice::top();

    BarrierStrength acqrel = BarrierStrength::AcqRel;
    BarrierStrength seqcst = BarrierStrength::SeqCst;
    [[maybe_unused]] bool seqcst_satisfies_acqrel = BarrierStrengthLattice::leq(acqrel, seqcst);
    [[maybe_unused]] BarrierStrength rj2 = BarrierStrengthLattice::join(acqrel, seqcst);
    [[maybe_unused]] BarrierStrength rm2 = BarrierStrengthLattice::meet(acqrel, seqcst);

    BarrierStrengthLattice::At<BarrierStrength::ReleaseStore>::element_type rel_pin{};
    [[maybe_unused]] BarrierStrength rel_recovered = rel_pin;

    OneByteValue payload{9};
    NoneGraded<OneByteValue> initial{payload, BarrierStrengthLattice::At<BarrierStrength::None>::bottom()};
    auto widened = initial.weaken(BarrierStrengthLattice::At<BarrierStrength::None>::top());
    auto composed = initial.compose(widened);
    [[maybe_unused]] auto grade = widened.grade();
    [[maybe_unused]] auto peeked = composed.peek().c;
}

}  // namespace detail::barrier_strength_lattice_self_test

}  // namespace foundation::algebra::lattices
