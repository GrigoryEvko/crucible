#pragma once

// Chain over how a function waits for a cross-thread event.  bottom is
// Block and top is SpinPause.  A cheaper wait is the stronger claim and
// sits higher, so leq(weak, strong) reads "a consumer that tolerates the
// weaker wait accepts a stronger provider": a spinning function is
// admissible at any waiter site because it never reaches the kernel.
// join takes the cheaper of two providers and meet the more expensive.
//
// The rungs rank the mechanism, not the observed wait.  The three lowest
// enter the kernel or the scheduler.  The three highest stay in user
// space.

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

enum class WaitStrategy : std::uint8_t {
    Block = 0,  // poll, epoll_wait, a blocking read
    Park = 1,  // pthread_cond_wait, std::condition_variable
    AcquireWait = 2,  // std::atomic::wait, futex(FUTEX_WAIT)
    UmwaitC01 = 3,  // umonitor and umwait, a power-aware user-space spin
    BoundedSpin = 4,  // SpinPause plus exponential backoff
    SpinPause = 5,  // _mm_pause or yield on an acquire load
};

inline constexpr std::size_t wait_strategy_count = ::foundation::reflect::enum_count<WaitStrategy>;

// The identifier of s, or "<unknown WaitStrategy>" for a value outside
// the enum.
[[nodiscard]] consteval std::string_view wait_strategy_name(WaitStrategy s) noexcept {
    return ::foundation::reflect::enum_name(s);
}

struct WaitLattice : ChainLatticeOps<WaitStrategy> {
    [[nodiscard]] static constexpr element_type bottom() noexcept { return WaitStrategy::Block; }
    [[nodiscard]] static constexpr element_type top() noexcept { return WaitStrategy::SpinPause; }

    [[nodiscard]] static consteval std::string_view name() noexcept { return "WaitLattice"; }

    template <WaitStrategy T>
    struct AtElement : PinnedElement<T> {
        using wait_strategy_value_type = WaitStrategy;
    };

    template <WaitStrategy T>
    struct At : PinnedAt<WaitLattice, T, AtElement<T>> {
        static constexpr WaitStrategy strategy = T;
    };
};

namespace wait_strategy {
using BlockStrategy = WaitLattice::At<WaitStrategy::Block>;
using ParkStrategy = WaitLattice::At<WaitStrategy::Park>;
using AcquireWaitStrategy = WaitLattice::At<WaitStrategy::AcquireWait>;
using UmwaitC01Strategy = WaitLattice::At<WaitStrategy::UmwaitC01>;
using BoundedSpinStrategy = WaitLattice::At<WaitStrategy::BoundedSpin>;
using SpinPauseStrategy = WaitLattice::At<WaitStrategy::SpinPause>;
}  // namespace wait_strategy

namespace detail::wait_lattice_self_test {

static_assert(wait_strategy_count == 6, "WaitStrategy catalog diverged from {Block, Park, AcquireWait, "
                                        "UmwaitC01, BoundedSpin, SpinPause}.  Confirm intent and update "
                                        "the wait-admission gates.");

static_assert(verify_chain_lattice<WaitLattice>(), "WaitLattice: the chain order, the pinned grades or the reflected "
                                                   "names diverged from the WaitStrategy enumerator list.");

static_assert(!UnboundedLattice<WaitLattice>);
static_assert(!Semiring<WaitLattice>);

static_assert(WaitLattice::bottom() == WaitStrategy::Block);
static_assert(WaitLattice::top() == WaitStrategy::SpinPause);

static_assert(WaitLattice::name() == "WaitLattice");
static_assert(wait_strategy::BlockStrategy::name() == "WaitLattice::At<Block>");
static_assert(wait_strategy::SpinPauseStrategy::name() == "WaitLattice::At<SpinPause>");
static_assert(WaitLattice::At<static_cast<WaitStrategy>(255)>::name() == "WaitLattice::At<?>");

static_assert(wait_strategy_name(WaitStrategy::UmwaitC01) == "UmwaitC01");
static_assert(wait_strategy_name(static_cast<WaitStrategy>(255)) == "<unknown WaitStrategy>");

static_assert(wait_strategy::BlockStrategy::strategy == WaitStrategy::Block);
static_assert(wait_strategy::SpinPauseStrategy::strategy == WaitStrategy::SpinPause);

struct OneByteValue {
    char c{0};
};
struct EightByteValue {
    unsigned long long v{0};
};

template <typename T_>
using SpinPauseGraded = Graded<ModalityKind::Absolute, wait_strategy::SpinPauseStrategy, T_>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(SpinPauseGraded, OneByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(SpinPauseGraded, EightByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(SpinPauseGraded, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(SpinPauseGraded, double);

template <typename T_>
using ParkGraded = Graded<ModalityKind::Absolute, wait_strategy::ParkStrategy, T_>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(ParkGraded, EightByteValue);

template <typename T_>
using BlockGraded = Graded<ModalityKind::Absolute, wait_strategy::BlockStrategy, T_>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(BlockGraded, EightByteValue);

// Static assertions alone can mask consteval, SFINAE and inline-body
// defects.  These calls pass non-constant arguments.
inline void runtime_smoke_test() {
    WaitStrategy a = WaitStrategy::Block;
    WaitStrategy b = WaitStrategy::SpinPause;
    [[maybe_unused]] bool l1 = WaitLattice::leq(a, b);
    [[maybe_unused]] WaitStrategy j1 = WaitLattice::join(a, b);
    [[maybe_unused]] WaitStrategy m1 = WaitLattice::meet(a, b);
    [[maybe_unused]] WaitStrategy bot = WaitLattice::bottom();
    [[maybe_unused]] WaitStrategy topv = WaitLattice::top();

    WaitStrategy umwait = WaitStrategy::UmwaitC01;
    WaitStrategy futex = WaitStrategy::AcquireWait;
    [[maybe_unused]] WaitStrategy j2 = WaitLattice::join(umwait, futex);
    [[maybe_unused]] WaitStrategy m2 = WaitLattice::meet(umwait, futex);

    OneByteValue v{42};
    SpinPauseGraded<OneByteValue> initial{v, wait_strategy::SpinPauseStrategy::bottom()};
    auto widened = initial.weaken(wait_strategy::SpinPauseStrategy::top());
    auto composed = initial.compose(widened);
    auto rv_widen = std::move(widened).weaken(wait_strategy::SpinPauseStrategy::top());

    [[maybe_unused]] auto g = rv_widen.grade();
    [[maybe_unused]] auto vc = composed.peek().c;

    wait_strategy::SpinPauseStrategy::element_type e{};
    [[maybe_unused]] WaitStrategy rec = e;
}

}  // namespace detail::wait_lattice_self_test

}  // namespace foundation::algebra::lattices
