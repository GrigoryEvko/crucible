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

inline constexpr std::size_t wait_strategy_count = std::meta::enumerators_of(^^WaitStrategy).size();

[[nodiscard]] consteval std::string_view wait_strategy_name(WaitStrategy s) noexcept {
    switch (s) {
        case WaitStrategy::Block:
            return "Block";
        case WaitStrategy::Park:
            return "Park";
        case WaitStrategy::AcquireWait:
            return "AcquireWait";
        case WaitStrategy::UmwaitC01:
            return "UmwaitC01";
        case WaitStrategy::BoundedSpin:
            return "BoundedSpin";
        case WaitStrategy::SpinPause:
            return "SpinPause";
        default:
            return std::string_view{"<unknown WaitStrategy>"};
    }
}

struct WaitLattice : ChainLatticeOps<WaitStrategy> {
    [[nodiscard]] static constexpr element_type bottom() noexcept { return WaitStrategy::Block; }
    [[nodiscard]] static constexpr element_type top() noexcept { return WaitStrategy::SpinPause; }

    [[nodiscard]] static consteval std::string_view name() noexcept { return "WaitLattice"; }

    template <WaitStrategy T>
    struct At {
        struct element_type {
            using wait_strategy_value_type = WaitStrategy;
            [[nodiscard]] constexpr operator wait_strategy_value_type() const noexcept { return T; }
            [[nodiscard]] constexpr bool operator==(element_type) const noexcept { return true; }
        };

        static constexpr WaitStrategy strategy = T;

        [[nodiscard]] static constexpr element_type bottom() noexcept { return {}; }
        [[nodiscard]] static constexpr element_type top() noexcept { return {}; }
        [[nodiscard]] static constexpr bool leq(element_type, element_type) noexcept { return true; }
        [[nodiscard]] static constexpr element_type join(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static constexpr element_type meet(element_type, element_type) noexcept { return {}; }

        [[nodiscard]] static consteval std::string_view name() noexcept {
            switch (T) {
                case WaitStrategy::Block:
                    return "WaitLattice::At<Block>";
                case WaitStrategy::Park:
                    return "WaitLattice::At<Park>";
                case WaitStrategy::AcquireWait:
                    return "WaitLattice::At<AcquireWait>";
                case WaitStrategy::UmwaitC01:
                    return "WaitLattice::At<UmwaitC01>";
                case WaitStrategy::BoundedSpin:
                    return "WaitLattice::At<BoundedSpin>";
                case WaitStrategy::SpinPause:
                    return "WaitLattice::At<SpinPause>";
                default:
                    return "WaitLattice::At<?>";
            }
        }
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

[[nodiscard]] consteval bool every_wait_strategy_has_name() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^WaitStrategy));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (wait_strategy_name([:en:]) == std::string_view{"<unknown WaitStrategy>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_wait_strategy_has_name(), "wait_strategy_name() switch missing an arm for at least one "
                                              "tier.  Add the arm or the new tier leaks the '<unknown "
                                              "WaitStrategy>' sentinel into diagnostic output.");

static_assert(Lattice<WaitLattice>);
static_assert(BoundedLattice<WaitLattice>);
static_assert(Lattice<wait_strategy::BlockStrategy>);
static_assert(Lattice<wait_strategy::ParkStrategy>);
static_assert(Lattice<wait_strategy::AcquireWaitStrategy>);
static_assert(Lattice<wait_strategy::UmwaitC01Strategy>);
static_assert(Lattice<wait_strategy::BoundedSpinStrategy>);
static_assert(Lattice<wait_strategy::SpinPauseStrategy>);
static_assert(BoundedLattice<wait_strategy::SpinPauseStrategy>);

static_assert(!UnboundedLattice<WaitLattice>);
static_assert(!Semiring<WaitLattice>);

static_assert(std::is_empty_v<wait_strategy::SpinPauseStrategy::element_type>);
static_assert(std::is_empty_v<wait_strategy::BoundedSpinStrategy::element_type>);
static_assert(std::is_empty_v<wait_strategy::ParkStrategy::element_type>);
static_assert(std::is_empty_v<wait_strategy::BlockStrategy::element_type>);

static_assert(verify_chain_lattice_exhaustive<WaitLattice>(),
              "WaitLattice chain-order lattice axioms fail at some triple.  The "
              "defect is in leq, join, meet or the enum encoding.");
static_assert(verify_chain_lattice_distributive_exhaustive<WaitLattice>(),
              "WaitLattice chain fails distributivity at some triple.");

static_assert(WaitLattice::leq(WaitStrategy::Block, WaitStrategy::Park));
static_assert(WaitLattice::leq(WaitStrategy::Park, WaitStrategy::AcquireWait));
static_assert(WaitLattice::leq(WaitStrategy::AcquireWait, WaitStrategy::UmwaitC01));
static_assert(WaitLattice::leq(WaitStrategy::UmwaitC01, WaitStrategy::BoundedSpin));
static_assert(WaitLattice::leq(WaitStrategy::BoundedSpin, WaitStrategy::SpinPause));
static_assert(WaitLattice::leq(WaitStrategy::Block, WaitStrategy::SpinPause));
static_assert(!WaitLattice::leq(WaitStrategy::SpinPause, WaitStrategy::Block));
static_assert(!WaitLattice::leq(WaitStrategy::SpinPause, WaitStrategy::BoundedSpin));
static_assert(!WaitLattice::leq(WaitStrategy::Park, WaitStrategy::Block));

static_assert(WaitLattice::bottom() == WaitStrategy::Block);
static_assert(WaitLattice::top() == WaitStrategy::SpinPause);

static_assert(WaitLattice::join(WaitStrategy::Block, WaitStrategy::SpinPause) == WaitStrategy::SpinPause);
static_assert(WaitLattice::join(WaitStrategy::Park, WaitStrategy::AcquireWait) == WaitStrategy::AcquireWait);
static_assert(WaitLattice::meet(WaitStrategy::Block, WaitStrategy::SpinPause) == WaitStrategy::Block);
static_assert(WaitLattice::meet(WaitStrategy::BoundedSpin, WaitStrategy::SpinPause) == WaitStrategy::BoundedSpin);

static_assert(WaitLattice::name() == "WaitLattice");
static_assert(wait_strategy::BlockStrategy::name() == "WaitLattice::At<Block>");
static_assert(wait_strategy::ParkStrategy::name() == "WaitLattice::At<Park>");
static_assert(wait_strategy::AcquireWaitStrategy::name() == "WaitLattice::At<AcquireWait>");
static_assert(wait_strategy::UmwaitC01Strategy::name() == "WaitLattice::At<UmwaitC01>");
static_assert(wait_strategy::BoundedSpinStrategy::name() == "WaitLattice::At<BoundedSpin>");
static_assert(wait_strategy::SpinPauseStrategy::name() == "WaitLattice::At<SpinPause>");

[[nodiscard]] consteval bool every_at_wait_strategy_has_name() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^WaitStrategy));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (WaitLattice::At<([:en:])>::name() == std::string_view{"WaitLattice::At<?>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_at_wait_strategy_has_name(), "WaitLattice::At<T>::name() switch missing an arm for at least "
                                                 "one strategy.");

static_assert(wait_strategy::BlockStrategy::strategy == WaitStrategy::Block);
static_assert(wait_strategy::ParkStrategy::strategy == WaitStrategy::Park);
static_assert(wait_strategy::AcquireWaitStrategy::strategy == WaitStrategy::AcquireWait);
static_assert(wait_strategy::UmwaitC01Strategy::strategy == WaitStrategy::UmwaitC01);
static_assert(wait_strategy::BoundedSpinStrategy::strategy == WaitStrategy::BoundedSpin);
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
