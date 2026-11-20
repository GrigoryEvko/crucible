// The compile-time checks of foundation/core/Immortal.h.

#include <foundation/core/Immortal.h>

#include <cstdint>
#include <type_traits>

namespace foundation::core {

namespace immortal_check {

// A payload with a destructor that is not trivial, as the window of the root
// of the allocator has.
struct Ending final {
    std::uint64_t word = 0;
    ~Ending() { word = 1; }
};

// An empty holder is a constant initializer, also for a payload with a
// destructor.
constinit inline Immortal<Ending> held{};

}  // namespace immortal_check

// The destructor of a holder is trivial for a payload with a destructor, so
// the destructor of the payload never runs (THE STORAGE).
static_assert(!std::is_trivially_destructible_v<immortal_check::Ending>);
static_assert(std::is_trivially_destructible_v<Immortal<immortal_check::Ending>>);

// The holder does not copy or move (THE RULES).
static_assert(!std::is_copy_constructible_v<Immortal<immortal_check::Ending>>);
static_assert(!std::is_move_constructible_v<Immortal<immortal_check::Ending>>);
static_assert(!std::is_copy_assignable_v<Immortal<immortal_check::Ending>>);

// A new holder is empty.
static_assert(!Immortal<std::uint64_t>{}.is_filled());

// The holder is the storage of its payload and one address.
static_assert(sizeof(Immortal<std::uint64_t>) == 2 * sizeof(std::uint64_t));
static_assert(alignof(Immortal<immortal_check::Ending>) == alignof(immortal_check::Ending));

// A payload is an object type that is not an array, not const and not
// volatile.
static_assert(ImmortalPayload<immortal_check::Ending>);
static_assert(!ImmortalPayload<std::uint64_t const>);
static_assert(!ImmortalPayload<std::uint64_t[2]>);
static_assert(!ImmortalPayload<std::uint64_t&>);

}  // namespace foundation::core
