#pragma once

// The base of a class that holds static members only, such as a door
// whose members are the mints of a layer.  No object of such a class
// exists.
//
// Each constructor of the base is deleted.  Each constructor of the
// derived class must construct the base, so the derived class cannot
// define a constructor of its own, and its implicit constructors are
// deleted.  The destructor of the base is user-provided.  The derived
// class is then not trivially copyable and not an implicit-lifetime type,
// so no byte route (std::bit_cast, std::start_lifetime_as) makes an object
// of it.
//
// Derive with the keyword `class` and no access specifier, which makes
// the base private.  A class with a public base can be an aggregate, and
// an aggregate is an implicit-lifetime type whatever its bases are.  With
// a private base the derived class is no aggregate, and a construction
// such as Door{} names the deleted constructor of Door.
//
// It is a template on the derived type, as foundation/Pinned.h is, so a
// refused construction names the derived type in the base too.

#include <foundation/Platform.h>

#include <type_traits>

namespace foundation {

template <typename Derived>
class NoObject {
public:
    NoObject() = delete("the class holds static members only, and no object of it exists");
    NoObject(const NoObject&) = delete("the class holds static members only, and no object of it exists");
    NoObject(NoObject&&) = delete("the class holds static members only, and no object of it exists");
    NoObject& operator=(const NoObject&) = delete("the class holds static members only, and no object of it exists");
    NoObject& operator=(NoObject&&) = delete("the class holds static members only, and no object of it exists");
    constexpr ~NoObject() noexcept {}
};

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
