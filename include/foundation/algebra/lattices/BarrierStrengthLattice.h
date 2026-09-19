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

#include <cstdint>
#include <meta>
#include <string_view>
#include <type_traits>

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

[[nodiscard]] consteval std::string_view barrier_strength_name(BarrierStrength k) noexcept {
    switch (k) {
        case BarrierStrength::None:
            return "None";
        case BarrierStrength::CompilerBarrier:
            return "CompilerBarrier";
        case BarrierStrength::AcquireLoad:
            return "AcquireLoad";
        case BarrierStrength::ReleaseStore:
            return "ReleaseStore";
        case BarrierStrength::AcqRel:
            return "AcqRel";
        case BarrierStrength::SeqCst:
            return "SeqCst";
        case BarrierStrength::FullFence:
            return "FullFence";
        default:
            return std::string_view{"<unknown BarrierStrength>"};
    }
}

struct BarrierStrengthLattice : ChainLatticeOps<BarrierStrength> {
    [[nodiscard]] static constexpr BarrierStrength bottom() noexcept { return BarrierStrength::None; }
    [[nodiscard]] static constexpr BarrierStrength top() noexcept { return BarrierStrength::FullFence; }
    [[nodiscard]] static consteval std::string_view name() noexcept { return "BarrierStrengthLattice"; }

    template <BarrierStrength K>
    struct At {
        struct element_type {
            using barrier_strength_value_type = BarrierStrength;
            [[nodiscard]] constexpr operator barrier_strength_value_type() const noexcept { return K; }
            [[nodiscard]] constexpr bool operator==(element_type) const noexcept { return true; }
        };
        static constexpr BarrierStrength tier = K;
        [[nodiscard]] static constexpr element_type bottom() noexcept { return {}; }
        [[nodiscard]] static constexpr element_type top() noexcept { return {}; }
        [[nodiscard]] static constexpr bool leq(element_type, element_type) noexcept { return true; }
        [[nodiscard]] static constexpr element_type join(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static constexpr element_type meet(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static consteval std::string_view name() noexcept {
            switch (K) {
                case BarrierStrength::None:
                    return "BarrierStrengthLattice::At<None>";
                case BarrierStrength::CompilerBarrier:
                    return "BarrierStrengthLattice::At<CompilerBarrier>";
                case BarrierStrength::AcquireLoad:
                    return "BarrierStrengthLattice::At<AcquireLoad>";
                case BarrierStrength::ReleaseStore:
                    return "BarrierStrengthLattice::At<ReleaseStore>";
                case BarrierStrength::AcqRel:
                    return "BarrierStrengthLattice::At<AcqRel>";
                case BarrierStrength::SeqCst:
                    return "BarrierStrengthLattice::At<SeqCst>";
                case BarrierStrength::FullFence:
                    return "BarrierStrengthLattice::At<FullFence>";
                default:
                    return "BarrierStrengthLattice::At<?>";
            }
        }
    };
};

namespace detail::barrier_strength_lattice_self_test {

inline constexpr std::size_t barrier_strength_count = std::meta::enumerators_of(^^BarrierStrength).size();

static_assert(barrier_strength_count == 7, "BarrierStrength diverged from {None, CompilerBarrier, AcquireLoad, "
                                           "ReleaseStore, AcqRel, SeqCst, FullFence}.  A new tier appends at "
                                           "the next free ordinal and needs the matching "
                                           "barrier_strength_name() arm and At<K>::name() arm.");

static_assert(std::to_underlying(BarrierStrength::None) == 0);

static_assert(std::to_underlying(BarrierStrength::FullFence) == 6);

static_assert(std::is_same_v<std::underlying_type_t<BarrierStrength>, std::uint8_t>);

[[nodiscard]] consteval bool every_barrier_strength_has_name() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^BarrierStrength));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        const auto candidate = barrier_strength_name([:en:]);
        if (candidate == std::string_view{"<unknown BarrierStrength>"}) return false;
        if (candidate.empty()) return false;
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_barrier_strength_has_name(), "barrier_strength_name() switch missing an arm for at least one "
                                                 "BarrierStrength enumerator.");

static_assert(::foundation::algebra::Lattice<BarrierStrengthLattice>);
static_assert(::foundation::algebra::BoundedLattice<BarrierStrengthLattice>);
static_assert(!::foundation::algebra::Semiring<BarrierStrengthLattice>);

static_assert(verify_chain_lattice_exhaustive<BarrierStrengthLattice>(),
              "BarrierStrengthLattice chain-order lattice axioms failed at some "
              "triple — leq/join/meet defect.");
static_assert(verify_chain_lattice_distributive_exhaustive<BarrierStrengthLattice>(),
              "BarrierStrengthLattice chain failed distributivity — leq/join/meet "
              "defect.");

static_assert(BarrierStrengthLattice::bottom() == BarrierStrength::None);
static_assert(BarrierStrengthLattice::top() == BarrierStrength::FullFence);

static_assert(BarrierStrengthLattice::name() == std::string_view{"BarrierStrengthLattice"});

static_assert(BarrierStrengthLattice::leq(BarrierStrength::None, BarrierStrength::FullFence));
static_assert(!BarrierStrengthLattice::leq(BarrierStrength::FullFence, BarrierStrength::None));

static_assert(BarrierStrengthLattice::leq(BarrierStrength::None, BarrierStrength::CompilerBarrier));
static_assert(BarrierStrengthLattice::leq(BarrierStrength::CompilerBarrier, BarrierStrength::AcquireLoad));
static_assert(BarrierStrengthLattice::leq(BarrierStrength::AcquireLoad, BarrierStrength::ReleaseStore));
static_assert(BarrierStrengthLattice::leq(BarrierStrength::ReleaseStore, BarrierStrength::AcqRel));
static_assert(BarrierStrengthLattice::leq(BarrierStrength::AcqRel, BarrierStrength::SeqCst));
static_assert(BarrierStrengthLattice::leq(BarrierStrength::SeqCst, BarrierStrength::FullFence));

static_assert(BarrierStrengthLattice::leq(BarrierStrength::AcqRel, BarrierStrength::SeqCst),
              "A SeqCst fence satisfies an AcqRel requirement.  leq(required, "
              "provided) is the admission direction.");
static_assert(!BarrierStrengthLattice::leq(BarrierStrength::SeqCst, BarrierStrength::AcqRel),
              "An AcqRel fence does not satisfy a SeqCst requirement.");

static_assert(!BarrierStrengthLattice::leq(BarrierStrength::CompilerBarrier, BarrierStrength::None));
static_assert(!BarrierStrengthLattice::leq(BarrierStrength::FullFence, BarrierStrength::SeqCst));

static_assert(BarrierStrengthLattice::join(BarrierStrength::CompilerBarrier, BarrierStrength::SeqCst)
              == BarrierStrength::SeqCst);
static_assert(BarrierStrengthLattice::join(BarrierStrength::None, BarrierStrength::AcquireLoad)
              == BarrierStrength::AcquireLoad);
static_assert(BarrierStrengthLattice::join(BarrierStrength::FullFence, BarrierStrength::AcqRel)
              == BarrierStrength::FullFence);

static_assert(BarrierStrengthLattice::meet(BarrierStrength::FullFence, BarrierStrength::AcquireLoad)
              == BarrierStrength::AcquireLoad);
static_assert(BarrierStrengthLattice::meet(BarrierStrength::None, BarrierStrength::SeqCst) == BarrierStrength::None);

static_assert(BarrierStrengthLattice::join(BarrierStrength::None, BarrierStrength::FullFence)
                  == BarrierStrength::FullFence,
              "join gives the strictest-wins reading on this chain, because the "
              "top is FullFence.  join(None, FullFence) returns FullFence, the "
              "stronger of the two.");
static_assert(BarrierStrengthLattice::meet(BarrierStrength::None, BarrierStrength::FullFence) == BarrierStrength::None,
              "meet gives the weakest floor, because the bottom is None.  An "
              "admission gate that grants only what every participant provides "
              "calls meet.");

static_assert(std::is_empty_v<BarrierStrengthLattice::At<BarrierStrength::None>::element_type>);
static_assert(std::is_empty_v<BarrierStrengthLattice::At<BarrierStrength::CompilerBarrier>::element_type>);
static_assert(std::is_empty_v<BarrierStrengthLattice::At<BarrierStrength::AcqRel>::element_type>);
static_assert(std::is_empty_v<BarrierStrengthLattice::At<BarrierStrength::FullFence>::element_type>);

static_assert(BarrierStrengthLattice::At<BarrierStrength::SeqCst>::tier == BarrierStrength::SeqCst);

[[nodiscard]] consteval bool every_at_barrier_strength_has_name() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^BarrierStrength));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (BarrierStrengthLattice::At<([:en:])>::name() == std::string_view{"BarrierStrengthLattice::At<?>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_at_barrier_strength_has_name(), "BarrierStrengthLattice::At<K>::name() switch missing an arm.");

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
