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
// alternative.  get<T>() lends the active alternative as an Option of a Ref
// of the Ref family, with no value for a different alternative.  is<T>()
// tells the active alternative, and which() gives its index as an Index<N>
// of the Region family.  The type has no operator*, no operator-> and no
// operator bool, and it is [[nodiscard]].
//
// The storage is the sum of the family (Choice.h).  The sum of plain
// alternatives holds the storage and a tag, and it is trivially copyable.
// A sum with an alternative that owns something is move-only.  Its move
// keeps the moved-from alternative in the source, which its destructor
// ends, so a borrow of the source stays a borrow of a live object.  A
// linear alternative takes the owning form, and only expect<T>() and the
// rvalue match() give it out.  get<T>() lends a linear alternative only as a
// const borrow.
//
// The includers of Choice.h do not read OneOf, so it has a header of its own.

#include <foundation/Brand.h>
#include <foundation/ByteSeal.h>
#include <foundation/Linear.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Index.h>
#include <foundation/core/Ref.h>
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
inline constexpr std::size_t index_in_v = detail::index_in_<T, Ts...>();

}  // namespace detail

template <class... Ts>
    requires OneOfAlternatives<Ts...>
class [[nodiscard]][[= ::foundation::linear::linear_when<HoldsLinearAlternative<Ts...>>]]
                   [[= ::foundation::lifetime::no_start_over_bytes{}]] OneOf {
    detail::SumSlot<Ts...> slot_;

    // The tag and the alternative at Index, through the door of the readers
    // of a sum slot (Choice.h).
    [[nodiscard]] constexpr std::size_t index_() const noexcept { return slot_.index_(detail::SumDoor::key()); }
    template <std::size_t Index>
    [[nodiscard]] constexpr auto& alternative_() noexcept {
        return slot_.template alternative_<Index>(detail::SumDoor::key());
    }
    template <std::size_t Index>
    [[nodiscard]] constexpr auto const& alternative_() const noexcept {
        return slot_.template alternative_<Index>(detail::SumDoor::key());
    }

    // The arm at Index takes the alternative at Index.  The last alternative
    // takes the last branch, so a match has no fatal path and no test of a
    // tag outside the alternatives.  Complexity: at most one compare for each
    // alternative.
    template <std::size_t Index, class Result, class... Arms>
    constexpr Result moved_from_(Arms&&... arms) && {
        if constexpr (Index + 1 == sizeof...(Ts)) {
            return static_cast<Arms...[Index]&&>(arms...[Index])(static_cast<Ts...[Index]&&>(alternative_<Index>()));
        } else {
            if (index_() == Index) {
                return static_cast<Arms...[Index]&&>(arms...[Index])(
                    static_cast<Ts...[Index]&&>(alternative_<Index>()));
            }
            return static_cast<OneOf&&>(*this).template moved_from_<Index + 1, Result>(static_cast<Arms&&>(arms)...);
        }
    }

    // The compare of the alternative at Index and after, when the two tags
    // are equal.  The last alternative takes the last branch.  Complexity:
    // at most one compare of a tag for each alternative, and one compare of
    // the active alternatives.
    template <std::size_t Index>
    [[nodiscard]] constexpr bool equal_from_(OneOf const& other) const noexcept {
        if constexpr (Index + 1 == sizeof...(Ts)) {
            return alternative_<Index>() == other.template alternative_<Index>();
        } else {
            if (index_() == Index) {
                return alternative_<Index>() == other.template alternative_<Index>();
            }
            return this->template equal_from_<Index + 1>(other);
        }
    }

    template <std::size_t Index, class Result, class... Arms>
    constexpr Result borrowed_from_(Arms&&... arms) const {
        if constexpr (Index + 1 == sizeof...(Ts)) {
            return static_cast<Arms...[Index]&&>(arms...[Index])(alternative_<Index>());
        } else {
            if (index_() == Index) {
                return static_cast<Arms...[Index]&&>(arms...[Index])(alternative_<Index>());
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

    // The index of the active alternative, as an Index of the Region family:
    // one load.  Each constructor of the slot writes the index of the
    // alternative that it builds, so the tag is below sizeof...(Ts) in each
    // state, and the door of Index needs no check.
    [[nodiscard]] constexpr Index<sizeof...(Ts)> which() const noexcept {
        return Index<sizeof...(Ts)>{detail::IndexDoor::key(), index_()};
    }

    // True when the active alternative is T.  The cost is one compare.
    template <class T>
        requires detail::AlternativeOf<T, Ts...>
    [[nodiscard]] constexpr bool is() const noexcept {
        return index_() == detail::index_in_v<T, Ts...>;
    }

    // The fatal unwrap at a boundary.  The reason tells why the active
    // alternative is T there.  A different alternative gives the reason and
    // the place of the call to fatal(), and the process ends.  A passing
    // expect() costs one branch.
    template <class T>
        requires detail::AlternativeOf<T, Ts...>
    [[nodiscard]] constexpr T expect(Fmt<> why) && noexcept {
        constexpr std::size_t index = detail::index_in_v<T, Ts...>;
        if (index_() != index) [[unlikely]] {
            fatal(why);
        }
        return static_cast<T&&>(alternative_<index>());
    }

    // The active alternative when it is T, as an Option of a Ref with the
    // fresh brand of the call, or no value for each other alternative: one
    // compare.  The OneOf keeps the alternative.  A linear alternative has
    // only the form of a const borrow, because a move through a Ref would
    // give out the alternative that the OneOf still holds.  The pack refuses
    // each explicit argument after T, so no caller names the brand, and the
    // result type holds the brand in undeducible_brand_t.  The
    // address of an alternative is not null, because a reference names an
    // object.  The build keeps each test of an address for null
    // (-fno-delete-null-pointer-checks), so the assumption states that fact,
    // and a match of the Option tests only the tag.  The condition reads a
    // local reference and no member, so a hardened build folds it too.
    template <class T, class... Refused, ::foundation::brand::IsBrand Fresh = CRUCIBLE_FRESH_BRAND>
        requires detail::AlternativeOf<T, Ts...> && (sizeof...(Refused) == 0) && DroppablePayload<T>
    [[nodiscard]] constexpr Option<Ref<T, ::foundation::brand::undeducible_brand_t<Fresh>>> get() & noexcept {
        constexpr std::size_t index = detail::index_in_v<T, Ts...>;
        if (index_() != index) return Option<Ref<T, Fresh>>{none};
        T& alternative = alternative_<index>();
        [[assume(__builtin_addressof(alternative) != nullptr)]];
        return Option<Ref<T, Fresh>>::some(detail::RefDoor::lend<Fresh>(alternative));
    }
    template <class T, class... Refused, ::foundation::brand::IsBrand Fresh = CRUCIBLE_FRESH_BRAND>
        requires detail::AlternativeOf<T, Ts...> && (sizeof...(Refused) == 0)
    [[nodiscard]] constexpr Option<Ref<T const, ::foundation::brand::undeducible_brand_t<Fresh>>>
    get() const& noexcept {
        constexpr std::size_t index = detail::index_in_v<T, Ts...>;
        if (index_() != index) return Option<Ref<T const, Fresh>>{none};
        T const& alternative = alternative_<index>();
        [[assume(__builtin_addressof(alternative) != nullptr)]];
        return Option<Ref<T const, Fresh>>::some(detail::RefDoor::lend<Fresh>(alternative));
    }
    template <class T, class... Refused>
        requires detail::AlternativeOf<T, Ts...>
    void get() && = delete("an alternative of a temporary OneOf dangles at the end of the full expression");
    template <class T, class... Refused>
        requires detail::AlternativeOf<T, Ts...>
    void get() const&& = delete("an alternative of a temporary OneOf dangles at the end of the full expression");

    // The total match: the arm at each position takes the alternative at
    // that position, the arm of the active alternative gets its value, and
    // each arm gives the same type.  A call with an arm too many or too few
    // does not compile, so no caller forgets an alternative.  The rvalue form
    // gives up the OneOf, which is often a temporary, so each arm takes its
    // alternative by value, and the result holds no borrow: a reference or an
    // address would dangle.  The conjunction reads the arms with the
    // alternatives only when the counts agree.
    template <class... Arms>
        requires(sizeof...(Arms) == sizeof...(Ts)) && (std::is_invocable_v<Arms, Ts &&> && ...)
             && (std::same_as<std::invoke_result_t<Arms, Ts &&>, std::invoke_result_t<Arms...[0], Ts...[0] &&>> && ...)
             && (TakesPayloadByValue<Arms, Ts> && ...) && HoldsNoBorrow<std::invoke_result_t<Arms...[0], Ts...[0]&&>>
    constexpr std::invoke_result_t<Arms...[0], Ts...[0]&&>
    match(Arms&&... arms) && noexcept((std::is_nothrow_invocable_v<Arms, Ts&&> && ...)) {
        using Result = std::invoke_result_t<Arms...[0], Ts...[0]&&>;
        return static_cast<OneOf&&>(*this).template moved_from_<0, Result>(static_cast<Arms&&>(arms)...);
    }

    // An arm of the rvalue match that takes its alternative by reference
    // binds to this overload, so the refusal gives its reason.
    template <class... Arms>
        requires(sizeof...(Arms) == sizeof...(Ts)) && (std::is_invocable_v<Arms, Ts &&> && ...)
                 && (std::same_as<std::invoke_result_t<Arms, Ts &&>, std::invoke_result_t<Arms...[0], Ts...[0] &&>>
                     && ...)
                 && (!(TakesPayloadByValue<Arms, Ts> && ...))
    void match(Arms&&... arms) && = delete("an arm of the rvalue match takes its alternative by value, so it names no "
                                           "object that the match ends: write the parameter as a value, or name the "
                                           "OneOf and match its borrow");

    // A result of the rvalue match that holds a borrow binds to this
    // overload, so the refusal gives its reason.
    template <class... Arms>
        requires(sizeof...(Arms) == sizeof...(Ts)) && (std::is_invocable_v<Arms, Ts &&> && ...)
                 && (std::same_as<std::invoke_result_t<Arms, Ts &&>, std::invoke_result_t<Arms...[0], Ts...[0] &&>>
                     && ...)
                 && (TakesPayloadByValue<Arms, Ts> && ...)
                 && (!HoldsNoBorrow<std::invoke_result_t<Arms...[0], Ts...[0] &&>>)
    void match(Arms&&... arms) && = delete("the result of the rvalue match holds no borrow, because each alternative "
                                           "ends in its arm: give a value with no reference and no address, or name "
                                           "the OneOf and match its borrow");

    // The total match of a borrow: the arm of the active alternative gets its
    // value as a const reference, and the OneOf keeps it.  The rule
    // borrow-match of the plugin refuses an arm that assigns, moves or changes
    // this OneOf.
    template <class... Arms>
        requires(sizeof...(Arms) == sizeof...(Ts)) && (std::is_invocable_v<Arms, Ts const&> && ...)
             && (std::same_as<std::invoke_result_t<Arms, Ts const&>, std::invoke_result_t<Arms...[0], Ts...[0] const&>>
                 && ...)
    [[= ::foundation::linear::borrow_match{}]] constexpr std::invoke_result_t<Arms...[0], Ts...[0] const&>
        match(Arms&&... arms) const& noexcept((std::is_nothrow_invocable_v<Arms, Ts const&> && ...)) {
        using Result = std::invoke_result_t<Arms...[0], Ts...[0] const&>;
        return this->template borrowed_from_<0, Result>(static_cast<Arms&&>(arms)...);
    }

    // A const rvalue of a OneOf that the borrow match would take binds to
    // this overload, so an arm cannot give a reference into an object that the
    // full expression ends.  An rvalue that is not const binds to the rvalue
    // match.  The constraint is that of the borrow match, so a call with the
    // wrong arms gets the refusal of its arms.
    template <class... Arms>
        requires(sizeof...(Arms) == sizeof...(Ts)) && (std::is_invocable_v<Arms, Ts const&> && ...)
                 && (std::same_as<std::invoke_result_t<Arms, Ts const&>,
                                  std::invoke_result_t<Arms...[0], Ts...[0] const&>>
                     && ...)
    void match(Arms&&... arms) const&& = delete("a borrow match of a temporary OneOf gives a reference into an object "
                                                "that the full expression ends: match the rvalue, or name the OneOf");

    // Two OneOfs of one type are equal when the same alternative is active
    // in each, with equal values.  The cost is one compare of the tags and
    // the walk of equal_from_.
    [[nodiscard]] friend constexpr bool operator==(OneOf const& left, OneOf const& right) noexcept
        requires(ComparablePart<Ts> && ...)
    {
        return left.index_() == right.index_() && left.template equal_from_<0>(right);
    }

    // Each other operand, such as a value of an alternative or a OneOf of
    // different types, binds to this overload, so no implicit constructor
    // makes a compare of two types.
    template <class Other>
        requires(!std::same_as<Other, OneOf>)
    friend bool operator==(OneOf const&, Other const&) =
        delete("a OneOf compares only with a OneOf of its own alternatives: test is<T>(), or match");
};

}  // namespace foundation::core
