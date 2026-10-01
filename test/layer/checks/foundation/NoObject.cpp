// The compile-time checks of foundation/NoObject.h.

#include <foundation/NoObject.h>

namespace foundation {

namespace detail::no_object_self_test {

class Door final : NoObject<Door> {
public:
    [[nodiscard]] static constexpr int answer() noexcept { return 42; }
};

// A public base makes an aggregate, which is an implicit-lifetime type.
// For this reason each door names the base with no access specifier.
struct PublicDoor final : NoObject<PublicDoor> {};

static_assert(Door::answer() == 42);
static_assert(!std::is_default_constructible_v<Door>);
static_assert(!std::is_copy_constructible_v<Door> && !std::is_move_constructible_v<Door>);
static_assert(!std::is_copy_assignable_v<Door> && !std::is_move_assignable_v<Door>);
static_assert(!std::is_trivially_copyable_v<Door>, "std::bit_cast must not make a door");
static_assert(!std::is_implicit_lifetime_v<Door>, "std::start_lifetime_as must not make a door");
static_assert(!std::is_aggregate_v<Door>);
static_assert(std::is_aggregate_v<PublicDoor> && std::is_implicit_lifetime_v<PublicDoor>);

}  // namespace detail::no_object_self_test

}  // namespace foundation
