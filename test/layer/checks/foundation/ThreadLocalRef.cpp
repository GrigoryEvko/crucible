// The compile-time checks of foundation/ThreadLocalRef.h.

#include <foundation/ThreadLocalRef.h>

namespace foundation {

namespace detail::thread_local_ref_self_test {

struct CounterTag {};
struct AccumulatorTag {};
struct OtherTag {};

using IntCounter = ThreadLocalRef<CounterTag, int>;
using IntAccumulator = ThreadLocalRef<AccumulatorTag, int>;
using DoubleOther = ThreadLocalRef<OtherTag, double>;

// The handle is an empty tag: it holds nothing, so it costs one byte and
// collapses to none as a member under [[no_unique_address]].
static_assert(sizeof(IntCounter) == 1 && sizeof(DoubleOther) == 1);
static_assert(std::is_empty_v<IntCounter> && std::is_trivially_copyable_v<IntCounter>);

inline constexpr IntCounter c_default{};
inline constexpr IntCounter c_copy = c_default;

static_assert(std::is_same_v<IntCounter::tag_type, CounterTag>);
static_assert(std::is_same_v<IntCounter::value_type, int>);
static_assert(std::is_same_v<DoubleOther::tag_type, OtherTag>);
static_assert(std::is_same_v<DoubleOther::value_type, double>);

static_assert(!std::is_same_v<IntCounter, IntAccumulator>);
static_assert(!std::is_same_v<IntCounter, DoubleOther>);

static_assert(std::is_copy_constructible_v<IntCounter> && std::is_copy_assignable_v<IntCounter>);
static_assert(std::is_move_constructible_v<IntCounter> && std::is_move_assignable_v<IntCounter>);

static_assert(IntCounter::value_type_name() == "int");
static_assert(DoubleOther::value_type_name() == "double");

}  // namespace detail::thread_local_ref_self_test

}  // namespace foundation
