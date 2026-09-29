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

enum class WaitStrategy : std::uint8_t {
    Block = 0,  // poll, epoll_wait, a blocking read
    Park = 1,  // pthread_cond_wait, std::condition_variable
    AcquireWait = 2,  // std::atomic::wait, futex(FUTEX_WAIT)
    UmwaitC01 = 3,  // umonitor and umwait, a power-aware user-space spin
    BoundedSpin = 4,  // SpinPause plus exponential backoff
    SpinPause = 5,  // _mm_pause or yield on an acquire load
};

// A cheaper wait is the stronger claim.
struct WaitLattice : EnumChainLattice<WaitLattice, WaitStrategy, ClaimOrientation::stronger_is_higher> {
    template <WaitStrategy T>
    struct At : PinnedAt<WaitLattice, T> {
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

static_assert(::foundation::reflect::enum_count<WaitStrategy> == 6,
              "WaitStrategy catalog diverged from {Block, Park, AcquireWait, UmwaitC01, BoundedSpin, SpinPause}.  "
              "Confirm intent and update the wait-admission gates.");

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

static_assert(wait_strategy::BlockStrategy::strategy == WaitStrategy::Block);
static_assert(wait_strategy::SpinPauseStrategy::strategy == WaitStrategy::SpinPause);

}  // namespace detail::wait_lattice_self_test

}  // namespace foundation::algebra::lattices
