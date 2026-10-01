// The compile-time checks of foundation/SwissTableBuffer.h.

#include <foundation/SwissTableBuffer.h>

namespace foundation {

namespace detail::swiss_table_buffer_self_test {

// A class that holds a proof, whose lifetime cannot start over bytes.
using ::foundation::lifetime::detail::HoldsProof;

static_assert(!std::is_copy_constructible_v<SwissTableBuffer<void*>>);
static_assert(!std::is_copy_assignable_v<SwissTableBuffer<void*>>);
static_assert(std::is_nothrow_move_constructible_v<SwissTableBuffer<void*>>);
static_assert(std::is_nothrow_move_assignable_v<SwissTableBuffer<void*>>);
static_assert(SwissTableSlot<const int*> && SwissTableSlot<std::uint64_t>);
static_assert(!SwissTableSlot<HoldsProof>, "a slot type whose subobject cannot start its lifetime is refused");
static_assert(!SwissTableSlot<int&>, "no lifetime start binds a reference slot");
static_assert(is_swiss_table_capacity(0) && is_swiss_table_capacity(16) && is_swiss_table_capacity(1024));
static_assert(!is_swiss_table_capacity(8) && !is_swiss_table_capacity(24) && !is_swiss_table_capacity(1)
              && !is_swiss_table_capacity(swiss_table_max_capacity * 2));

}  // namespace detail::swiss_table_buffer_self_test

}  // namespace foundation
