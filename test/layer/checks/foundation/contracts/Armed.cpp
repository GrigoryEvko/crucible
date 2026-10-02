// The compile-time checks of foundation/contracts/Armed.h.  The checks of
// the cells over a stand-in roster are in the check file of
// foundation/contracts/ArmedRoster.h, beside the walk that reads them.

#include <foundation/contracts/Armed.h>

namespace foundation::contracts {

namespace detail::armed_self_test {

template <class T>
struct always_true : std::true_type {};

template <class T>
struct always_false : std::false_type {};

template <class T>
struct is_int : std::bool_constant<std::is_same_v<T, int>> {};

static_assert(predicate_accepts<is_int, int>());
static_assert(predicate_refuses<is_int, char, void>());
static_assert(!predicate_accepts<is_int, char>());
static_assert(!predicate_refuses<is_int, int>());

// An empty witness pack proves nothing, so neither helper answers yes.
static_assert(!predicate_accepts<always_true>());
static_assert(!predicate_refuses<always_false>());

// The two unarmed shapes, each caught by exactly one of the pair.
static_assert(!predicate_accepts<always_false, int>());
static_assert(!predicate_refuses<always_true, int>());

}  // namespace detail::armed_self_test

}  // namespace foundation::contracts
