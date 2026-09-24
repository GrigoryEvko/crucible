#pragma once

#include <crucible/Platform.h>

#include <type_traits>

namespace crucible::safety {

template <typename T>
concept NotInherited = std::is_final_v<T>;

// Preferred over a bare finality assertion at the use site, because the
// bracketed tag in the message is what an audit greps for.
template <typename T>
consteval void assert_not_inherited() noexcept {
    static_assert(std::is_final_v<T>, "[NotInherited_Not_Final] crucible::safety::assert_not_inherited<T>: "
                                      "T is not marked `final`. Either mark T `final` at its declaration, "
                                      "or inherit virtually from crucible::safety::FinalBy<T> to prevent "
                                      "extension structurally.");
}

// Derived must inherit VIRTUALLY, as `public virtual FinalBy<Derived>`.  That
// is what makes this work and the template cannot enforce it.  Virtual
// inheritance moves responsibility for constructing the base to the most-derived
// class, so a class deriving from Derived must construct FinalBy itself and is
// rejected by the private constructor, of which only Derived is a friend.  A
// non-virtual base leaves Derived as the constructor of its own direct base no
// matter how far the chain is extended, and prevents nothing.
//
// A subclass's implicit copy or move reaches the deleted operations below, so
// the tag in their reason strings lands in secondary diagnostics too.
//
// The cost is the derived object's virtual-base offset.  Prefer the `final`
// keyword where the author of the type can be relied on to write it.
template <typename Derived>
class FinalBy {
private:
    constexpr FinalBy() noexcept = default;
    ~FinalBy() = default;

    FinalBy(const FinalBy&) = delete(
        "[FinalBy_Subclass_Forbidden] FinalBy<Derived>: copy of the CRTP base is forbidden; subclassing a FinalBy-protected type is not allowed. Mark Derived final, or stop trying to extend it.");
    FinalBy(FinalBy&&) = delete(
        "[FinalBy_Subclass_Forbidden] FinalBy<Derived>: move of the CRTP base is forbidden; subclassing a FinalBy-protected type is not allowed. Mark Derived final, or stop trying to extend it.");
    FinalBy& operator=(const FinalBy&) = delete(
        "[FinalBy_Subclass_Forbidden] FinalBy<Derived>: copy-assignment of the CRTP base is forbidden; subclassing a FinalBy-protected type is not allowed. Mark Derived final, or stop trying to extend it.");
    FinalBy& operator=(FinalBy&&) = delete(
        "[FinalBy_Subclass_Forbidden] FinalBy<Derived>: move-assignment of the CRTP base is forbidden; subclassing a FinalBy-protected type is not allowed. Mark Derived final, or stop trying to extend it.");

    friend Derived;
};

namespace detail {
struct FinalByEboTag {};
}  // namespace detail
static_assert(sizeof(FinalBy<detail::FinalByEboTag>) == sizeof(char),
              "FinalBy<T> must be an empty class — "
              "otherwise it would add direct bytes to Derived.");
static_assert(std::is_empty_v<FinalBy<detail::FinalByEboTag>>, "FinalBy<T> must satisfy std::is_empty_v.");

}  // namespace crucible::safety
