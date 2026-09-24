#pragma once

#include <crucible/safety/MemOrder.h>
#include <crucible/safety/IsMemOrder.h>

#include <type_traits>

namespace crucible::fixy::sync {

using Order = ::crucible::safety::MemOrderTag_v;

template <Order O, typename T>
using MemOrder = ::crucible::safety::MemOrder<O, T>;

using MemOrderLattice = ::crucible::safety::MemOrderLattice;

template <typename T>
inline constexpr bool is_mem_order_v = ::crucible::safety::extract::is_mem_order_v<T>;

template <typename T>
concept IsMemOrder = ::crucible::safety::extract::IsMemOrder<T>;

template <typename T>
    requires is_mem_order_v<T>
using mem_order_value_t = ::crucible::safety::extract::mem_order_value_t<T>;

template <typename T>
    requires is_mem_order_v<T>
inline constexpr Order mem_order_tag_v = ::crucible::safety::extract::mem_order_tag_v<T>;

namespace mem_order {
template <typename T>
using Relaxed = MemOrder<Order::Relaxed, T>;
template <typename T>
using Acquire = MemOrder<Order::Acquire, T>;
template <typename T>
using Release = MemOrder<Order::Release, T>;
template <typename T>
using AcqRel = MemOrder<Order::AcqRel, T>;
template <typename T>
using SeqCst = MemOrder<Order::SeqCst, T>;
}  // namespace mem_order

namespace detail::mem_order_surface_sentinel {

static_assert(std::is_same_v<::crucible::fixy::sync::MemOrder<Order::Relaxed, int>,
                             ::crucible::safety::MemOrder<::crucible::safety::MemOrderTag_v::Relaxed, int>>,
              "MemOrder<O, T> must alias the substrate wrapper verbatim.");

static_assert(std::is_same_v<::crucible::fixy::sync::MemOrder<Order::SeqCst, double>,
                             ::crucible::safety::MemOrder<::crucible::safety::MemOrderTag_v::SeqCst, double>>,
              "MemOrder<SeqCst, double> must alias the substrate wrapper verbatim.");

static_assert(std::is_same_v<Order, ::crucible::safety::MemOrderTag_v>,
              "Order must alias the substrate memory-order tag enum.");

// The ordinals carry the chain order, so the values themselves are
// load-bearing. The chain runs heaviest fence first: SeqCst is the
// bottom because it makes the weakest claim about what a consumer may
// assume, and Relaxed is the top because it claims no fence is needed.
static_assert(static_cast<int>(Order::SeqCst) == 0);
static_assert(static_cast<int>(Order::AcqRel) == 1);
static_assert(static_cast<int>(Order::Release) == 2);
static_assert(static_cast<int>(Order::Acquire) == 3);
static_assert(static_cast<int>(Order::Relaxed) == 4);

static_assert(MemOrderLattice::leq(Order::SeqCst, Order::Relaxed), "SeqCst ⊑ Relaxed must hold.");
static_assert(!MemOrderLattice::leq(Order::Relaxed, Order::SeqCst),
              "Relaxed ⊑ SeqCst must fail. The chain direction has drifted.");

static_assert(sizeof(MemOrder<Order::Relaxed, int>) == sizeof(int),
              "MemOrder<Relaxed, int> must collapse to sizeof(int).");
static_assert(sizeof(MemOrder<Order::SeqCst, double>) == sizeof(double),
              "MemOrder<SeqCst, double> must collapse to sizeof(double).");

static_assert(IsMemOrder<MemOrder<Order::Relaxed, int>>);
static_assert(IsMemOrder<MemOrder<Order::SeqCst, double>>);
static_assert(!IsMemOrder<int>);
static_assert(!IsMemOrder<int*>);

static_assert(std::is_same_v<mem_order_value_t<MemOrder<Order::Relaxed, int>>, int>);
static_assert(std::is_same_v<mem_order_value_t<MemOrder<Order::SeqCst, double>>, double>);
static_assert(mem_order_tag_v<MemOrder<Order::Relaxed, int>> == Order::Relaxed);
static_assert(mem_order_tag_v<MemOrder<Order::SeqCst, int>> == Order::SeqCst);

static_assert(std::is_same_v<mem_order::Relaxed<int>, MemOrder<Order::Relaxed, int>>);
static_assert(std::is_same_v<mem_order::Acquire<int>, MemOrder<Order::Acquire, int>>);
static_assert(std::is_same_v<mem_order::Release<int>, MemOrder<Order::Release, int>>);
static_assert(std::is_same_v<mem_order::AcqRel<int>, MemOrder<Order::AcqRel, int>>);
static_assert(std::is_same_v<mem_order::SeqCst<int>, MemOrder<Order::SeqCst, int>>);

// Subsumption runs up the chain, so a Relaxed-pinned value satisfies
// every requirement below it. A SeqCst-pinned value satisfies only
// SeqCst: it carries a total-order dependency that a consumer written
// against a weaker requirement would drop.
static_assert(MemOrder<Order::Relaxed, int>::template satisfies<Order::Relaxed>);
static_assert(MemOrder<Order::Relaxed, int>::template satisfies<Order::Acquire>);
static_assert(MemOrder<Order::Relaxed, int>::template satisfies<Order::SeqCst>);
static_assert(MemOrder<Order::SeqCst, int>::template satisfies<Order::SeqCst>);
static_assert(!MemOrder<Order::SeqCst, int>::template satisfies<Order::Relaxed>,
              "A SeqCst-pinned value must not satisfy a Relaxed requirement. "
              "If it did, a seq_cst-fenced value would flow silently through a "
              "gate that requires the relaxed discipline.");

// There is no Consume tier. P3475R2 deprecates memory_order::consume.
static_assert(std::meta::enumerators_of(^^Order).size() == 5,
              "The memory-order enumerator count has drifted. The mem_order "
              "convenience aliases must mirror every tier.");

}  // namespace detail::mem_order_surface_sentinel

}  // namespace crucible::fixy::sync
