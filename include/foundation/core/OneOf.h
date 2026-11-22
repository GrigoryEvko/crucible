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
// of a const T, with no value for a different alternative.  The one road
// that writes the active alternative in place is the frame door
// one_of.with_mut(arms...) of foundation/core/Frame.h.  is<T>()
// tells the active alternative, and which() gives its index as an Index<N>
// of the Region family, after one compare of the tag with the count.  The
// type has no operator*, no operator-> and no operator bool, and it is
// [[nodiscard]].
//
// Only a copy of bytes writes a tag that names no alternative.  A read that
// must name the active alternative then ends the process: which(), each
// match, and the compare of two OneOfs with the same tag.  is<T>() and get<T>()
// compare the tag with one index, so such a tag gives false and no value.
//
// The storage is the sum of the family (Choice.h).  The sum of plain
// alternatives holds the storage and a tag, and its copy, move and end are
// those of its bytes.  It is trivially copyable when each alternative is,
// and a sealed alternative gives it an assignment that copies the bytes.
// A sum with an alternative that owns something is move-only.  Its move
// keeps the moved-from alternative in the source, which its destructor
// ends, so a borrow of the source stays a borrow of a live object.  A
// linear alternative takes the owning form, and only expect<T>() and the
// rvalue match() give it out.  get<T>() lends each alternative only as a
// const borrow, and the frame door refuses a linear alternative.
//
// The includers of Choice.h do not read OneOf, so it has a header of its own.

#include <foundation/Brand.h>
#include <foundation/ByteSeal.h>
#include <foundation/Linear.h>
#include <foundation/Platform.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Frame.h>
#include <foundation/core/Index.h>
#include <foundation/core/Ref.h>
#include <foundation/core/Report.h>
#include <foundation/reflect/TypeList.h>

#include <concepts>
#include <cstddef>
#include <type_traits>

namespace foundation::core {

namespace detail {

// The fatal exit of a tag that names no alternative of a OneOf.  The call
// takes no argument, so each inlined read of a tag adds only a compare and a
// branch to one call.  The name starts with report_, as each cold fatal exit
// of the family does, so the rule codegen-shapes reads a call of it as a
// fatal path.
[[noreturn]] CRUCIBLE_COLD inline void report_forged_one_of_tag_() noexcept {
    fatal("OneOf: the tag names no alternative, so a copy of bytes made this OneOf");
}

}  // namespace detail

template <class... Ts>
    requires OneOfAlternatives<Ts...>
class [[nodiscard]][[= ::foundation::linear::linear_when<HoldsLinearAlternative<Ts...>>]]
                   [[= ::foundation::linear::sum{}]][[= ::foundation::lifetime::no_start_over_bytes{}]] OneOf {
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

    // THE WALKS OF THE TAG.  The match and the compare walk the tag from the
    // first index.  Each step compares the tag with its index, the last step
    // too.  A tag after the last index names no alternative, because only a
    // copy of bytes writes it.  The walk then ends the process and runs no
    // arm, so no arm reads the storage as the last alternative.  The compare
    // at the last step is one compare and one branch to a cold call, and the
    // optimizer folds it when it knows the tag.  Complexity: at most one
    // compare for each alternative.

    // The arm at Index takes the alternative at Index (THE WALKS OF THE TAG).
    template <std::size_t Index, class Result, class... Arms>
    constexpr Result moved_from_(Arms&&... arms) && {
        if (index_() == Index) {
            return static_cast<Arms...[Index]&&>(arms...[Index])(static_cast<Ts...[Index]&&>(alternative_<Index>()));
        }
        if constexpr (Index + 1 == sizeof...(Ts)) {
            detail::report_forged_one_of_tag_();
        } else {
            return static_cast<OneOf&&>(*this).template moved_from_<Index + 1, Result>(static_cast<Arms&&>(arms)...);
        }
    }

    // The compare of the alternative at Index and after, when the two tags
    // are equal (THE WALKS OF THE TAG).  Complexity: at most one compare of a
    // tag for each alternative, and one compare of the active alternatives.
    template <std::size_t Index>
    [[nodiscard]] constexpr bool equal_from_(OneOf const& other) const noexcept {
        if (index_() == Index) {
            return ::foundation::core::equal(alternative_<Index>(), other.template alternative_<Index>());
        }
        if constexpr (Index + 1 == sizeof...(Ts)) {
            detail::report_forged_one_of_tag_();
        } else {
            return this->template equal_from_<Index + 1>(other);
        }
    }

    // The arm at Index takes a borrow of the alternative at Index (THE WALKS
    // OF THE TAG).
    template <std::size_t Index, class Result, class... Arms>
    constexpr Result borrowed_from_(Arms&&... arms) const {
        if (index_() == Index) {
            return static_cast<Arms...[Index]&&>(arms...[Index])(alternative_<Index>());
        }
        if constexpr (Index + 1 == sizeof...(Ts)) {
            detail::report_forged_one_of_tag_();
        } else {
            return this->template borrowed_from_<Index + 1, Result>(static_cast<Arms&&>(arms)...);
        }
    }

    // The arm at Index of the frame door takes the RefMut of the alternative
    // at Index, with the brand of the frame (THE WALKS OF THE TAG).
    template <std::size_t Index, class Result, class Brand, class... Arms>
    constexpr Result written_from_(Arms&&... arms) {
        if (index_() == Index) {
            return static_cast<Arms...[Index]&&>(arms...[Index])(
                RefMut<Ts... [Index], Brand> { detail::FrameDoor::key(), alternative_<Index>() });
        }
        if constexpr (Index + 1 == sizeof...(Ts)) {
            detail::report_forged_one_of_tag_();
        } else {
            return this->template written_from_<Index + 1, Result, Brand>(static_cast<Arms&&>(arms)...);
        }
    }

public:
    // The alternative of the type of the value.  The conversion is implicit,
    // and the type of the value must be one of Ts, so no conversion of the
    // value selects an alternative.
    template <class U>
        requires ::foundation::reflect::OccursIn<std::remove_cvref_t<U>, Ts...>
              && std::constructible_from<std::remove_cvref_t<U>, U&&>
    constexpr OneOf(U&& value) noexcept(std::is_nothrow_constructible_v<std::remove_cvref_t<U>, U&&>)
        : slot_{detail::AlternativeAt<::foundation::reflect::index_of_v<std::remove_cvref_t<U>, Ts...>>{},
                static_cast<U&&>(value)} {}

    // The index of the active alternative, as an Index of the Region family.
    // Each constructor of the slot writes the index of the alternative that
    // it builds.  But a copy of bytes over a plain OneOf, such as a copy that
    // a byte buffer or a union member makes, can write a tag outside the
    // alternatives, and a read through the Index would then leave its range.
    // So which() compares the tag with the count one time, and a tag that
    // names no alternative ends the process.
    [[nodiscard]] constexpr Index<sizeof...(Ts)> which() const noexcept {
        std::size_t const tag = index_();
        if (tag >= sizeof...(Ts)) [[unlikely]] {
            detail::report_forged_one_of_tag_();
        }
        // The compare above proves the tag below the count, so the optimizer
        // removes the check of the Index.
        return Index<sizeof...(Ts)>::check(tag).expect("the tag of a OneOf names an alternative");
    }

    // True when the active alternative is T.  The cost is one compare.
    template <class T>
        requires ::foundation::reflect::OccursIn<T, Ts...>
    [[nodiscard]] constexpr bool is() const noexcept {
        return index_() == ::foundation::reflect::index_of_v<T, Ts...>;
    }

    // The fatal unwrap at a boundary.  The reason tells why the active
    // alternative is T there.  A different alternative gives the reason and
    // the place of the call to fatal(), and the process ends.  A passing
    // expect() costs one branch.
    template <class T>
        requires ::foundation::reflect::OccursIn<T, Ts...>
    [[nodiscard]] constexpr T expect(Fmt<> why) && noexcept {
        constexpr std::size_t index = ::foundation::reflect::index_of_v<T, Ts...>;
        if (index_() != index) [[unlikely]] {
            fatal(why);
        }
        return static_cast<T&&>(alternative_<index>());
    }

    // The active alternative when it is T, as an Option of a Ref of a const
    // T with the fresh brand of the call, or no value for each other
    // alternative: one compare.  The OneOf keeps the alternative.  The Ref
    // reads the alternative, also of a OneOf that is not const.  The frame
    // door with_mut writes the active alternative in place
    // (foundation/core/Frame.h).  The pack refuses each explicit argument
    // after T, so no caller names the brand, and the result type holds the
    // brand in undeducible_brand_t.  The address of an alternative is not
    // null, because a reference names an object.  The build keeps each test
    // of an address for null (-fno-delete-null-pointer-checks), so the
    // assumption states that fact, and a match of the Option tests only the
    // tag.  The condition reads a local reference and no member, so a
    // hardened build folds it too.
    template <class T, class... Refused, ::foundation::brand::IsBrand Fresh = CRUCIBLE_FRESH_BRAND>
        requires ::foundation::reflect::OccursIn<T, Ts...> && (sizeof...(Refused) == 0)
    [[nodiscard]] constexpr Option<Ref<T const, ::foundation::brand::undeducible_brand_t<Fresh>>>
    get() const& noexcept {
        constexpr std::size_t index = ::foundation::reflect::index_of_v<T, Ts...>;
        if (index_() != index) return Option<Ref<T const, Fresh>>{none};
        T const& alternative = alternative_<index>();
        [[assume(__builtin_addressof(alternative) != nullptr)]];
        return Option<Ref<T const, Fresh>>::some(detail::RefDoor::lend<Fresh>(alternative));
    }
    template <class T, class... Refused>
        requires ::foundation::reflect::OccursIn<T, Ts...>
    void get() && = delete("an alternative of a temporary OneOf dangles at the end of the full expression");
    template <class T, class... Refused>
        requires ::foundation::reflect::OccursIn<T, Ts...>
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
    // value as a const reference, and the OneOf keeps it.  The parameter of
    // each arm is the alternative at its position or a const reference to
    // it, so no conversion reads an alternative as another type, and arms in
    // the wrong order do not compile.  The rule borrow-match of the plugin
    // refuses an arm that assigns, moves or changes this OneOf, and an owning
    // OneOf counts the lend (THE LEND COUNT of Choice.h).
    template <class... Arms>
        requires(sizeof...(Arms) == sizeof...(Ts)) && (std::is_invocable_v<Arms, Ts const&> && ...)
             && (std::same_as<std::invoke_result_t<Arms, Ts const&>, std::invoke_result_t<Arms...[0], Ts...[0] const&>>
                 && ...)
             && (TakesPayloadByBorrow<Arms, Ts> && ...)
    [[= ::foundation::linear::borrow_match{}]] constexpr std::invoke_result_t<Arms...[0], Ts...[0] const&>
        match(Arms&&... arms) const& noexcept((std::is_nothrow_invocable_v<Arms, Ts const&> && ...)) {
        using Result = std::invoke_result_t<Arms...[0], Ts...[0] const&>;
        [[maybe_unused]] auto const lend = slot_.lend_(detail::SumDoor::key());
        return this->template borrowed_from_<0, Result>(static_cast<Arms&&>(arms)...);
    }

    // An arm of the borrow match that takes another type binds to this
    // overload, so the refusal gives its reason.
    template <class... Arms>
        requires(sizeof...(Arms) == sizeof...(Ts)) && (std::is_invocable_v<Arms, Ts const&> && ...)
                 && (std::same_as<std::invoke_result_t<Arms, Ts const&>,
                                  std::invoke_result_t<Arms...[0], Ts...[0] const&>>
                     && ...)
                 && (!(TakesPayloadByBorrow<Arms, Ts> && ...))
    void match(Arms&&... arms) const& = delete("an arm of the borrow match takes the alternative at its position, as a "
                                               "value or a const reference, so no conversion reads an alternative as "
                                               "another type: write the parameter as that alternative");

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
                 && (TakesPayloadByBorrow<Arms, Ts> && ...)
    void match(Arms&&... arms) const&& = delete("a borrow match of a temporary OneOf gives a reference into an object "
                                                "that the full expression ends: match the rvalue, or name the OneOf");

    // The frame door of the active alternative (foundation/core/Frame.h): the
    // arm of the active alternative gets its RefMut in place, and the door
    // gives the result of the arm.  The arm at each position takes the
    // alternative at that position, and each arm gives the same type.  The
    // OneOf keeps the alternative, and the door writes no byte of the OneOf.
    // The rule borrow-match refuses an arm that assigns, moves or changes
    // this OneOf, and an owning OneOf counts the lend (THE LEND COUNT of
    // Choice.h).  A linear alternative has no frame door, because an arm
    // could move it out through the RefMut and leave the OneOf with it.  Each
    // explicit argument goes to the pack of the arms, so no caller names the
    // brand.  Complexity: at most one compare of the tag for each
    // alternative.
    template <class... Arms, ::foundation::brand::IsBrand Fresh = CRUCIBLE_SCOPE_BRAND>
        requires(sizeof...(Arms) == sizeof...(Ts)) && (DroppablePayload<Ts> && ...)
             && (std::is_nothrow_invocable_v<Arms &&, RefMut<Ts, Fresh>> && ...)
             && (std::same_as<std::invoke_result_t<Arms &&, RefMut<Ts, Fresh>>,
                              std::invoke_result_t<Arms...[0] &&, RefMut<Ts...[0], Fresh>>>
                 && ...)
             && FrameResult<std::invoke_result_t<Arms...[0]&&, RefMut<Ts...[0], Fresh>>, Fresh>
    [[= ::foundation::linear::borrow_match{}]] constexpr std::invoke_result_t<Arms...[0]&&, RefMut<Ts...[0], Fresh>>
        with_mut(Arms&&... arms) & noexcept {
        using Result = std::invoke_result_t<Arms...[0]&&, RefMut<Ts...[0], Fresh>>;
        [[maybe_unused]] auto const lend = slot_.lend_(detail::SumDoor::key());
        return this->template written_from_<0, Result, Fresh>(static_cast<Arms&&>(arms)...);
    }

    // Arms whose result holds a borrow or names the brand of the frame bind
    // to this overload, so the refusal gives its reason.
    template <class... Arms, ::foundation::brand::IsBrand Fresh = CRUCIBLE_SCOPE_BRAND>
        requires(sizeof...(Arms) == sizeof...(Ts)) && (DroppablePayload<Ts> && ...)
                 && (std::is_nothrow_invocable_v<Arms &&, RefMut<Ts, Fresh>> && ...)
                 && (std::same_as<std::invoke_result_t<Arms &&, RefMut<Ts, Fresh>>,
                                  std::invoke_result_t<Arms...[0] &&, RefMut<Ts...[0], Fresh>>>
                     && ...)
                 && (!FrameResult<std::invoke_result_t<Arms...[0] &&, RefMut<Ts...[0], Fresh>>, Fresh>)
    void with_mut(Arms&&... arms) & = delete("the result of a frame door leaves the frame, so it holds no reference "
                                             "and no address, and it names no brand of the frame: give a plain value");

    template <class... Arms>
    void with_mut(Arms&&... arms) && = delete("a frame door of a temporary OneOf writes an alternative that the full "
                                              "expression ends: name the OneOf");

    // Two OneOfs of one type are equal when the same alternative is active
    // in each, with equal values.  The cost is one compare of the tags and
    // the walk of equal_from_.
    [[nodiscard]] friend constexpr bool operator==(OneOf const& left, OneOf const& right) noexcept
        requires(Equatable<Ts> && ...)
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
