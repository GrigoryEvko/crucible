#pragma once

// The Link family: an optional borrow of one object, with the brand of the
// store of the object, which gives the object only as const through a const
// Link.
//
// A Link<T, Brand> refers to one object of T or to no object, as an
// Option<Ref<T, Brand>> does, and it takes one pointer.  The brand names the
// store of the object, such as the arena family of a graph
// (foundation/core/BumpArena.h).  It is a fresh brand and never the erased
// brand DefaultBrand, and no constructor changes it, so a Ref of a different
// store does not go into the Link.  A Link that is not const gives a
// Ref<T, Brand>.  A const Link gives only a Ref<T const, Brand>.  So a reader
// of a const object that holds a Link writes nothing through the Link, as a
// reader of a Rust struct through a shared borrow writes nothing through a
// field that is a mutable borrow.  A Link only borrows its object, so it
// dangles when the owner of the object ends.
//
//   is_some(), is_none()      true when the Link refers to an object, or not
//   option()                  the Link as an Option of a Ref: a Ref<T, Brand>
//                             from a Link that is not const, a
//                             Ref<T const, Brand> from a const Link
//   expect(why)               the Ref, or the end of the process with why
//   match(on_some, on_none)   on_some with the Ref, or on_none
//   is_some_and(holds)        true when the Link refers to an object and
//                             holds gives true for its Ref<T const, Brand>
//   for (Ref<T, Brand> object : link)
//                             one pass over the object, or no pass
//
// A Link copies only from a Link that is not const, as a mutable borrow of
// Rust moves but does not copy out of a shared borrow.  So no copy of a const
// Link writes the object.  A Link states refers_deep{} (foundation/Transfer.h).
// Two threads can read one const Link when two threads can read the object.
// A copy of a Link that is not const refers to the same object and can write
// it, so a Link is Sendable only when two threads can write the object.

#include <foundation/Brand.h>
#include <foundation/Transfer.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Ref.h>

#include <type_traits>
#include <utility>

namespace foundation::core {

// The cursor of the pass over a Link.  It holds a copy of the Ref, so a pass
// over a temporary Link reads no object that the full expression ends.
template <class T, ::foundation::brand::IsFreshBrand Brand>
class LinkCursor final {
public:
    constexpr LinkCursor() noexcept = default;
    explicit constexpr LinkCursor(Option<Ref<T, Brand>> at) noexcept : at_{at} {}

    // The Ref of the pass.  A cursor at the end refers to no object, and a
    // read of it ends the process.
    [[nodiscard]] constexpr Ref<T, Brand> operator*() const noexcept {
        return Option<Ref<T, Brand>>{at_}.expect("a cursor at the end of a Link refers to no object");
    }

    constexpr LinkCursor& operator++() noexcept {
        at_ = none;
        return *this;
    }

    // Two cursors are equal when both refer to an object or neither does.  A
    // pass compares a cursor only with the end.
    [[nodiscard]] friend constexpr bool operator==(LinkCursor const& left, LinkCursor const& right) noexcept {
        return left.at_.is_some() == right.at_.is_some();
    }

private:
    Option<Ref<T, Brand>> at_{};
};

template <class T, ::foundation::brand::IsFreshBrand Brand>
class[[= ::foundation::transfer::refers_deep{}]] Link final {
public:
    // A Link that refers to no object.
    constexpr Link() noexcept = default;
    constexpr Link(NoValue /*none*/) noexcept {}

    // A Link to the object of the Ref, or to no object.  The conversion is
    // implicit, so some(ref) and a Ref<T, Brand> give a Link.  The Ref keeps
    // its brand, so a Ref of a different brand gives no Link.
    constexpr Link(Option<Ref<T, Brand>> target) noexcept : target_{target} {}
    constexpr Link(Ref<T, Brand> target) noexcept : target_{some(target)} {}

    // A copy takes a Link that is not const.  No constructor takes a const
    // Link, so a class that holds a Link copies only from an object that is
    // not const too.
    constexpr Link(Link& other) noexcept = default;
    constexpr Link(Link&& other) noexcept = default;
    constexpr Link& operator=(Link& other) noexcept = default;
    constexpr Link& operator=(Link&& other) noexcept = default;
    constexpr ~Link() = default;

    [[nodiscard]] constexpr bool is_some() const noexcept { return target_.is_some(); }
    [[nodiscard]] constexpr bool is_none() const noexcept { return target_.is_none(); }

    // The Link as an Option of a Ref.  A const Link gives a Ref of const.
    [[nodiscard]] constexpr Option<Ref<T, Brand>> option() & noexcept { return target_; }
    [[nodiscard]] constexpr Option<Ref<T, Brand>> option() && noexcept { return target_; }
    [[nodiscard]] constexpr Option<Ref<T const, Brand>> option() const& noexcept {
        return target_.match([](Ref<T, Brand> const& object) noexcept { return some(Ref<T const, Brand>{object}); },
                             []() noexcept { return Option<Ref<T const, Brand>>{}; });
    }

    // The Ref, or the end of the process with the reason why.  A const Link
    // gives a Ref of const.
    [[nodiscard]] constexpr Ref<T, Brand> expect(Fmt<> why) & noexcept { return option().expect(why); }
    [[nodiscard]] constexpr Ref<T, Brand> expect(Fmt<> why) && noexcept { return option().expect(why); }
    [[nodiscard]] constexpr Ref<T const, Brand> expect(Fmt<> why) const& noexcept { return option().expect(why); }

    // on_some with the Ref, or on_none.  A const Link gives on_some a Ref of
    // const.  The match borrows a copy of the Link, so on_some takes the Ref
    // by const reference or by value.  The match calls one arm, and a call is
    // no consume, so the arms hold no linear value by value
    // (ArmsHoldNoLinearValue of foundation/core/Choice.h), and each match has
    // a deleted twin for arms that hold one.
    template <class OnSome, class OnNone>
        requires std::is_invocable_v<OnSome, Ref<T, Brand> const&> && std::is_invocable_v<OnNone>
              && ArmsHoldNoLinearValue<OnSome, OnNone>
    constexpr auto match(OnSome&& on_some, OnNone&& on_none) & {
        const Option<Ref<T, Brand>> target = target_;
        return target.match(std::forward<OnSome>(on_some), std::forward<OnNone>(on_none));
    }
    template <class OnSome, class OnNone>
        requires std::is_invocable_v<OnSome, Ref<T, Brand> const&> && std::is_invocable_v<OnNone>
              && ArmsHoldNoLinearValue<OnSome, OnNone>
    constexpr auto match(OnSome&& on_some, OnNone&& on_none) && {
        const Option<Ref<T, Brand>> target = target_;
        return target.match(std::forward<OnSome>(on_some), std::forward<OnNone>(on_none));
    }
    template <class OnSome, class OnNone>
        requires std::is_invocable_v<OnSome, Ref<T const, Brand> const&> && std::is_invocable_v<OnNone>
              && ArmsHoldNoLinearValue<OnSome, OnNone>
    constexpr auto match(OnSome&& on_some, OnNone&& on_none) const& {
        const Option<Ref<T const, Brand>> target = option();
        return target.match(std::forward<OnSome>(on_some), std::forward<OnNone>(on_none));
    }

    // Arms that hold a linear value by value bind to these overloads, so the
    // refusal gives its reason.
    template <class OnSome, class OnNone>
        requires(!ArmsHoldNoLinearValue<OnSome, OnNone>)
                 && std::is_invocable_v<OnSome, Ref<T, Brand> const&> && std::is_invocable_v<OnNone>
    void match(OnSome&& on_some, OnNone&& on_none) & = delete(CRUCIBLE_LINEAR_BODY_REFUSAL);
    template <class OnSome, class OnNone>
        requires(!ArmsHoldNoLinearValue<OnSome, OnNone>)
                 && std::is_invocable_v<OnSome, Ref<T, Brand> const&> && std::is_invocable_v<OnNone>
    void match(OnSome&& on_some, OnNone&& on_none) && = delete(CRUCIBLE_LINEAR_BODY_REFUSAL);
    template <class OnSome, class OnNone>
        requires(!ArmsHoldNoLinearValue<OnSome, OnNone>)
                 && std::is_invocable_v<OnSome, Ref<T const, Brand> const&> && std::is_invocable_v<OnNone>
    void match(OnSome&& on_some, OnNone&& on_none) const& = delete(CRUCIBLE_LINEAR_BODY_REFUSAL);

    // True when the Link refers to an object and holds gives true for it.
    template <class Holds>
        requires std::is_invocable_r_v<bool, Holds&, Ref<T const, Brand> const&>
    [[nodiscard]] constexpr bool is_some_and(Holds&& holds) const {
        return option().is_some_and(holds);
    }

    // One pass over the object, or no pass.  A const Link gives a Ref of
    // const.
    [[nodiscard]] constexpr LinkCursor<T, Brand> begin() & noexcept { return LinkCursor<T, Brand>{target_}; }
    [[nodiscard]] constexpr LinkCursor<T, Brand> end() & noexcept { return LinkCursor<T, Brand>{}; }
    [[nodiscard]] constexpr LinkCursor<T, Brand> begin() && noexcept { return LinkCursor<T, Brand>{target_}; }
    [[nodiscard]] constexpr LinkCursor<T, Brand> end() && noexcept { return LinkCursor<T, Brand>{}; }
    [[nodiscard]] constexpr LinkCursor<T const, Brand> begin() const& noexcept {
        return LinkCursor<T const, Brand>{option()};
    }
    [[nodiscard]] constexpr LinkCursor<T const, Brand> end() const& noexcept { return LinkCursor<T const, Brand>{}; }

private:
    Option<Ref<T, Brand>> target_{};
};

}  // namespace foundation::core
