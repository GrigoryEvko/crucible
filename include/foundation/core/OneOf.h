#pragma once

// The Choice family: one value of a closed set of types.
//
// OneOf<Ts...> holds a value of exactly one of its alternatives Ts.  It has
// no default constructor and no empty state, so each OneOf holds a value.  A
// value of one of Ts converts to a OneOf with no conversion of its type, so
// `return code;` and `return count;` give a OneOf.  The gate
// OneOfAlternatives of Choice.h admits 2 to 255 payloads with no type two
// times, so each value selects one alternative.
//
// The roads to a value are the total match(), with one arm for each
// alternative in the order of Ts, and expect<T>(), the fatal unwrap of one
// alternative.  is<T>() tells the active alternative.  The type has no
// operator*, no operator-> and no operator bool, and it is [[nodiscard]].
//
// The storage is the sum of the family (Choice.h).  The sum of plain
// alternatives holds the storage and a tag, and it is trivially copyable.
// A sum with an alternative that owns something is move-only.  Its move
// keeps the moved-from alternative in the source, which its destructor
// ends, so a borrow of the source stays a borrow of a live object.  A
// linear alternative takes the owning form, and only expect<T>() and the
// rvalue match() give it out.
//
// which(), the index of the active alternative as an Index<N> of the Region
// family, and get<T>(), a borrow that can miss, are planned.  The includers
// of Choice.h do not read OneOf, so it has a header of its own.

#include <foundation/ByteSeal.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Report.h>

#include <concepts>
#include <cstddef>
#include <type_traits>

namespace foundation::core {

namespace detail {

// T is one of Ts.
template <class T, class... Ts>
concept AlternativeOf = (std::is_same_v<T, Ts> || ...);

// The index of T in Ts.  The gate of OneOf holds each type one time at
// most.  Complexity: linear in the count of Ts.
template <class T, class... Ts>
[[nodiscard]] consteval std::size_t index_in_() noexcept {
    bool const matches[] = {std::is_same_v<T, Ts>...};
    std::size_t index = 0;
    while (index < sizeof...(Ts) && !matches[index])
        ++index;
    return index;
}

template <class T, class... Ts>
inline constexpr std::size_t index_in_v = index_in_<T, Ts...>();

}  // namespace detail

template <class... Ts>
    requires OneOfAlternatives<Ts...>
class [[nodiscard]][[= ::foundation::lifetime::no_start_over_bytes{}]] OneOf {
    detail::SumSlot<Ts...> slot_;

    // The arm at Index takes the alternative at Index.  The last alternative
    // takes the last branch, so a match has no fatal path and no test of a
    // tag outside the alternatives.  Complexity: at most one compare for each
    // alternative.
    template <std::size_t Index, class Result, class... Arms>
    constexpr Result moved_from_(Arms&&... arms) && {
        if constexpr (Index + 1 == sizeof...(Ts)) {
            return static_cast<Arms...[Index]&&>(arms...[Index])(
                static_cast<Ts...[Index]&&>(slot_.template alternative_<Index>()));
        } else {
            if (slot_.index_() == Index) {
                return static_cast<Arms...[Index]&&>(arms...[Index])(
                    static_cast<Ts...[Index]&&>(slot_.template alternative_<Index>()));
            }
            return static_cast<OneOf&&>(*this).template moved_from_<Index + 1, Result>(static_cast<Arms&&>(arms)...);
        }
    }

    template <std::size_t Index, class Result, class... Arms>
    constexpr Result borrowed_from_(Arms&&... arms) const {
        if constexpr (Index + 1 == sizeof...(Ts)) {
            return static_cast<Arms...[Index]&&>(arms...[Index])(slot_.template alternative_<Index>());
        } else {
            if (slot_.index_() == Index) {
                return static_cast<Arms...[Index]&&>(arms...[Index])(slot_.template alternative_<Index>());
            }
            return this->template borrowed_from_<Index + 1, Result>(static_cast<Arms&&>(arms)...);
        }
    }

public:
    // The alternative of the type of the value.  The conversion is implicit,
    // and the type of the value must be one of Ts, so no conversion of the
    // value selects an alternative.
    template <class U>
        requires detail::AlternativeOf<std::remove_cvref_t<U>, Ts...>
              && std::constructible_from<std::remove_cvref_t<U>, U&&>
    constexpr OneOf(U&& value) noexcept(std::is_nothrow_constructible_v<std::remove_cvref_t<U>, U&&>)
        : slot_{detail::AlternativeAt<detail::index_in_v<std::remove_cvref_t<U>, Ts...>>{}, static_cast<U&&>(value)} {}

    // True when the active alternative is T.  The cost is one compare.
    template <class T>
        requires detail::AlternativeOf<T, Ts...>
    [[nodiscard]] constexpr bool is() const noexcept {
        return slot_.index_() == detail::index_in_v<T, Ts...>;
    }

    // The fatal unwrap at a boundary.  The reason tells why the active
    // alternative is T there.  A different alternative gives the reason and
    // the place of the call to fatal(), and the process ends.  A passing
    // expect() costs one branch.
    template <class T>
        requires detail::AlternativeOf<T, Ts...>
    [[nodiscard]] constexpr T expect(Fmt<> why) && noexcept {
        constexpr std::size_t index = detail::index_in_v<T, Ts...>;
        if (slot_.index_() != index) [[unlikely]] {
            fatal(why);
        }
        return static_cast<T&&>(slot_.template alternative_<index>());
    }

    // The total match: the arm at each position takes the alternative at
    // that position, the arm of the active alternative gets its value, and
    // each arm gives the same type.  A call with an arm too many or too few
    // does not compile, so no caller forgets an alternative.  The rvalue form
    // gives up the OneOf, which is often a temporary, so an arm that gives a
    // reference is refused: the reference would dangle.  The conjunction
    // reads the arms with the alternatives only when the counts agree.
    template <class... Arms>
        requires(sizeof...(Arms) == sizeof...(Ts)) && (std::is_invocable_v<Arms, Ts &&> && ...)
             && (std::same_as<std::invoke_result_t<Arms, Ts &&>, std::invoke_result_t<Arms...[0], Ts...[0] &&>> && ...)
             && (!std::is_reference_v<std::invoke_result_t<Arms...[0], Ts...[0] &&>>)
    constexpr std::invoke_result_t<Arms...[0], Ts...[0]&&>
    match(Arms&&... arms) && noexcept((std::is_nothrow_invocable_v<Arms, Ts&&> && ...)) {
        using Result = std::invoke_result_t<Arms...[0], Ts...[0]&&>;
        return static_cast<OneOf&&>(*this).template moved_from_<0, Result>(static_cast<Arms&&>(arms)...);
    }

    // The total match of a borrow: the arm of the active alternative gets its
    // value as a const reference, and the OneOf keeps it.
    template <class... Arms>
        requires(sizeof...(Arms) == sizeof...(Ts)) && (std::is_invocable_v<Arms, Ts const&> && ...)
             && (std::same_as<std::invoke_result_t<Arms, Ts const&>, std::invoke_result_t<Arms...[0], Ts...[0] const&>>
                 && ...)
    constexpr std::invoke_result_t<Arms...[0], Ts...[0] const&>
    match(Arms&&... arms) const& noexcept((std::is_nothrow_invocable_v<Arms, Ts const&> && ...)) {
        using Result = std::invoke_result_t<Arms...[0], Ts...[0] const&>;
        return this->template borrowed_from_<0, Result>(static_cast<Arms&&>(arms)...);
    }

    // A temporary OneOf that the borrow match would take binds to this
    // overload, so an arm cannot give a reference into an object that the
    // full expression ends.  The constraint is that of the borrow match, so a
    // call with the wrong arms gets the refusal of its arms.
    template <class... Arms>
        requires(sizeof...(Arms) == sizeof...(Ts)) && (std::is_invocable_v<Arms, Ts const&> && ...)
                 && (std::same_as<std::invoke_result_t<Arms, Ts const&>,
                                  std::invoke_result_t<Arms...[0], Ts...[0] const&>>
                     && ...)
    void match(Arms&&... arms) const&& = delete("a borrow match of a temporary OneOf gives a reference into an object "
                                                "that the full expression ends: match the rvalue, or name the OneOf");
};

}  // namespace foundation::core
