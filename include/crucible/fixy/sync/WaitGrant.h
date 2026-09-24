#pragma once

#include <crucible/safety/_Wait.h>
#include <crucible/safety/IsWait.h>

#include <type_traits>

namespace crucible::fixy::sync {

using Strategy = ::crucible::safety::WaitStrategy_v;

template <Strategy S, typename T>
using Wait = ::crucible::safety::Wait<S, T>;

using WaitLattice = ::crucible::safety::WaitLattice;

template <typename T>
inline constexpr bool is_wait_v = ::crucible::safety::extract::is_wait_v<T>;

template <typename T>
concept IsWait = ::crucible::safety::extract::IsWait<T>;

template <typename T>
    requires is_wait_v<T>
using wait_value_t = ::crucible::safety::extract::wait_value_t<T>;

template <typename T>
    requires is_wait_v<T>
inline constexpr Strategy wait_strategy_v = ::crucible::safety::extract::wait_strategy_v<T>;

namespace wait {
template <typename T>
using SpinPause = Wait<Strategy::SpinPause, T>;
template <typename T>
using BoundedSpin = Wait<Strategy::BoundedSpin, T>;
template <typename T>
using UmwaitC01 = Wait<Strategy::UmwaitC01, T>;
template <typename T>
using AcquireWait = Wait<Strategy::AcquireWait, T>;
template <typename T>
using Park = Wait<Strategy::Park, T>;
template <typename T>
using Block = Wait<Strategy::Block, T>;
}  // namespace wait

namespace detail::wait_surface_sentinel {

static_assert(std::is_same_v<::crucible::fixy::sync::Wait<Strategy::SpinPause, int>,
                             ::crucible::safety::Wait<::crucible::safety::WaitStrategy_v::SpinPause, int>>,
              "Wait<S, T> must alias the substrate wrapper verbatim.");

static_assert(std::is_same_v<::crucible::fixy::sync::Wait<Strategy::Block, double>,
                             ::crucible::safety::Wait<::crucible::safety::WaitStrategy_v::Block, double>>,
              "Wait<Block, double> must alias the substrate wrapper verbatim.");

static_assert(std::is_same_v<Strategy, ::crucible::safety::WaitStrategy_v>,
              "Strategy must alias the substrate wait-strategy enum.");

// The ordinals carry the chain order, so the values themselves are
// load-bearing.
static_assert(static_cast<int>(Strategy::SpinPause) == 5);
static_assert(static_cast<int>(Strategy::BoundedSpin) == 4);
static_assert(static_cast<int>(Strategy::UmwaitC01) == 3);
static_assert(static_cast<int>(Strategy::AcquireWait) == 2);
static_assert(static_cast<int>(Strategy::Park) == 1);
static_assert(static_cast<int>(Strategy::Block) == 0);

static_assert(WaitLattice::leq(Strategy::Block, Strategy::SpinPause), "Block ⊑ SpinPause must hold.");
static_assert(!WaitLattice::leq(Strategy::SpinPause, Strategy::Block),
              "SpinPause ⊑ Block must fail. The chain direction has drifted.");

static_assert(sizeof(Wait<Strategy::SpinPause, int>) == sizeof(int),
              "Wait<SpinPause, int> must collapse to sizeof(int).");
static_assert(sizeof(Wait<Strategy::Block, double>) == sizeof(double),
              "Wait<Block, double> must collapse to sizeof(double).");

static_assert(IsWait<Wait<Strategy::SpinPause, int>>);
static_assert(IsWait<Wait<Strategy::Park, double>>);
static_assert(!IsWait<int>);
static_assert(!IsWait<int*>);

static_assert(std::is_same_v<wait_value_t<Wait<Strategy::SpinPause, int>>, int>);
static_assert(std::is_same_v<wait_value_t<Wait<Strategy::Park, double>>, double>);
static_assert(wait_strategy_v<Wait<Strategy::SpinPause, int>> == Strategy::SpinPause);
static_assert(wait_strategy_v<Wait<Strategy::Block, int>> == Strategy::Block);

static_assert(std::is_same_v<wait::SpinPause<int>, Wait<Strategy::SpinPause, int>>);
static_assert(std::is_same_v<wait::BoundedSpin<int>, Wait<Strategy::BoundedSpin, int>>);
static_assert(std::is_same_v<wait::UmwaitC01<int>, Wait<Strategy::UmwaitC01, int>>);
static_assert(std::is_same_v<wait::AcquireWait<int>, Wait<Strategy::AcquireWait, int>>);
static_assert(std::is_same_v<wait::Park<int>, Wait<Strategy::Park, int>>);
static_assert(std::is_same_v<wait::Block<int>, Wait<Strategy::Block, int>>);

static_assert(Wait<Strategy::SpinPause, int>::template satisfies<Strategy::SpinPause>);
static_assert(Wait<Strategy::SpinPause, int>::template satisfies<Strategy::BoundedSpin>);
static_assert(Wait<Strategy::SpinPause, int>::template satisfies<Strategy::Block>);
static_assert(Wait<Strategy::Block, int>::template satisfies<Strategy::Block>);
static_assert(!Wait<Strategy::Block, int>::template satisfies<Strategy::SpinPause>,
              "A Block-tier value must not satisfy a SpinPause requirement. "
              "If it did, a futex-tier or syscall-tier wait would flow "
              "silently through a gate that requires a spin.");

static_assert(std::meta::enumerators_of(^^Strategy).size() == 6,
              "The wait-strategy enumerator count has drifted. The wait "
              "convenience aliases must mirror every tier.");

}  // namespace detail::wait_surface_sentinel

}  // namespace crucible::fixy::sync
