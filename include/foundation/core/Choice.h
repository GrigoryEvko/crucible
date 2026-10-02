#pragma once

// The Choice family: a value that can be absent.
//
// Option<T> holds one T or no value.  The only roads to the payload are a
// loop of zero or one turns, the total match(), value_or(), and expect(),
// the fatal unwrap with a reason.  The type has no operator* and no
// operator->, so a read of an empty Option is not possible.  The whole
// type is [[nodiscard]].
//
// The storage has three forms, and the payload type selects the form:
//
//   niche    The payload declares an empty value through niche<T>.  The
//            Option then has the size of the payload, and the empty value
//            is the state "no value".
//   plain    A trivially copyable payload with no niche.  The Option holds
//            the payload and one flag, and it is trivially copyable.  The
//            x86-64 System V ABI then gives it back in registers when it
//            has at most 16 bytes.
//   owning   Each other payload.  The Option is move-only.  A move leaves
//            the source empty, and the destructor destroys the payload.
//
// A move of a niche or plain Option is a copy of the bytes, so the source
// keeps its value.  The payload owns nothing in those two forms, so two
// copies of it are safe.  expect(), value_or() and the rvalue match()
// empty the source in each form.
//
// The families of foundation/core use only the language, the compiler
// builtins and the headers of the base allowance.  fixy/Core.h puts their
// public names into namespace fixy.  A check of a family does the same
// work in each build and with each contract semantic, and it ends the
// process through fatal() of the Report family.

#include <foundation/ByteSeal.h>
#include <foundation/core/Report.h>

#include <concepts>
#include <cstddef>
#include <new>
#include <type_traits>

namespace foundation::core {

// The state "no value".  The default constructor is explicit, so a braced
// list with no elements does not convert to it.
struct NoValue final {
    explicit constexpr NoValue() noexcept = default;
};

inline constexpr NoValue none{};

// The payload of an Option: an object type that is not an array, not
// cv-qualified and not the empty marker itself.  The move and the
// destructor do not throw, so a move never leaves a half-moved Option.
template <class T>
concept ChoicePayload =
    std::is_object_v<T> && !std::is_array_v<T> && !std::is_const_v<T> && !std::is_volatile_v<T>
    && !std::same_as<T, NoValue> && std::is_nothrow_move_constructible_v<T> && std::is_nothrow_destructible_v<T>;

template <ChoicePayload T>
class Option;

// The niche protocol.  A payload type T with an empty value that no valid
// T holds can specialize niche<T> with two static members:
//
//   static constexpr T empty() noexcept;              the empty value
//   static constexpr bool is_empty(T const&) noexcept;
//
// The primary template has no members, so a type with no specialization
// has no niche.  A niche payload has trivial copy and move constructors and
// a trivial destructor, so the ABI passes the Option in registers.  Its
// assignment can be user-provided, as the assignment of a type that holds
// a byte seal is.
template <class T>
struct niche {};

template <class T>
concept HasNiche =
    std::is_trivially_copy_constructible_v<T> && std::is_trivially_move_constructible_v<T>
    && std::is_trivially_destructible_v<T> && std::is_nothrow_copy_assignable_v<T> && requires(T const& value) {
           { niche<T>::empty() } noexcept -> std::same_as<T>;
           { niche<T>::is_empty(value) } noexcept -> std::same_as<bool>;
       };

namespace detail {

struct NoPayload final {};

// The dispatch tag of the constructor that builds a full Option.
struct SomeTag final {
    explicit constexpr SomeTag() noexcept = default;
};

enum class OptionForm : unsigned char {
    niche,
    plain,
    owning,
};

template <class T>
inline constexpr OptionForm option_form_of =
    HasNiche<T> ? OptionForm::niche
                : (std::is_trivially_copyable_v<T> && std::is_trivially_destructible_v<T> ? OptionForm::plain
                                                                                          : OptionForm::owning);

template <class T, OptionForm Form = option_form_of<T>>
class OptionSlot;

template <class T>
class OptionSlot<T, OptionForm::niche> {
    T value_ = niche<T>::empty();

public:
    constexpr OptionSlot() noexcept = default;

    template <class U>
    constexpr OptionSlot(SomeTag, U&& value) noexcept(std::is_nothrow_constructible_v<T, U&&>)
        : value_(static_cast<U&&>(value)) {
        // A payload equal to the empty value would read as no value.  A
        // payload that cannot hold the empty value, such as a View that its
        // door bounds, gives the optimizer what it needs to remove the check.
        if (niche<T>::is_empty(value_)) [[unlikely]] {
            fatal("Option::some got the empty value of the niche of its payload");
        }
    }

    [[nodiscard]] constexpr bool holds_() const noexcept { return !niche<T>::is_empty(value_); }
    [[nodiscard]] constexpr T& payload_() noexcept { return value_; }
    [[nodiscard]] constexpr T const& payload_() const noexcept { return value_; }
    constexpr void clear_() noexcept { value_ = niche<T>::empty(); }
};

// The special members are implicit, and each one is trivial, because each
// member of the union is trivially copyable.
template <class T>
class OptionSlot<T, OptionForm::plain> {
    union {
        NoPayload empty_;
        T value_;
    };
    bool is_engaged_ = false;

public:
    constexpr OptionSlot() noexcept : empty_{} {}

    template <class U>
    constexpr OptionSlot(SomeTag, U&& value) noexcept(std::is_nothrow_constructible_v<T, U&&>)
        : value_(static_cast<U&&>(value)), is_engaged_{true} {}

    [[nodiscard]] constexpr bool holds_() const noexcept { return is_engaged_; }
    [[nodiscard]] constexpr T& payload_() noexcept { return value_; }
    [[nodiscard]] constexpr T const& payload_() const noexcept { return value_; }
    constexpr void clear_() noexcept {
        empty_ = NoPayload{};
        is_engaged_ = false;
    }
};

template <class T>
class OptionSlot<T, OptionForm::owning> {
    union {
        NoPayload empty_;
        T value_;
    };
    bool is_engaged_ = false;

public:
    constexpr OptionSlot() noexcept : empty_{} {}

    template <class U>
    constexpr OptionSlot(SomeTag, U&& value) noexcept(std::is_nothrow_constructible_v<T, U&&>)
        : value_(static_cast<U&&>(value)), is_engaged_{true} {}

    // The payload moves into the new slot, and the source becomes empty.
    constexpr OptionSlot(OptionSlot&& other) noexcept : empty_{} {
        if (other.is_engaged_) {
            ::new(static_cast<void*>(&value_)) T(static_cast<T&&>(other.value_));
            is_engaged_ = true;
            other.clear_();
        }
    }

    OptionSlot(OptionSlot const&) =
        delete("an owning Option is move-only: a copy would duplicate what the payload owns");
    OptionSlot& operator=(OptionSlot const&) = delete("an owning Option is move-only");
    OptionSlot& operator=(OptionSlot&&) = delete("an owning Option is not assignable: build a new Option instead");

    constexpr ~OptionSlot() {
        if (is_engaged_) value_.~T();
    }

    [[nodiscard]] constexpr bool holds_() const noexcept { return is_engaged_; }
    [[nodiscard]] constexpr T& payload_() noexcept { return value_; }
    [[nodiscard]] constexpr T const& payload_() const noexcept { return value_; }
    constexpr void clear_() noexcept {
        if (is_engaged_) {
            value_.~T();
            is_engaged_ = false;
        }
    }
};

}  // namespace detail

// The position of a loop over an Option.  It points at the payload, or it
// is null at the end, so a loop has zero turns or one turn.  The seal
// refuses std::bit_cast and a lifetime start over bytes, so a cursor comes
// only from begin() and end().
template <class Element>
class OptionCursor {
    Element* at_ = nullptr;
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

    template <ChoicePayload T>
    friend class Option;

    constexpr explicit OptionCursor(Element* at) noexcept : at_{at} {}

public:
    // A loop reads the cursor only after a compare with the end, so the
    // optimizer removes this check there.
    [[nodiscard]] constexpr Element& operator*() const noexcept {
        if (at_ == nullptr) [[unlikely]] {
            fatal("a read through the end cursor of an Option");
        }
        return *at_;
    }

    constexpr OptionCursor& operator++() noexcept {
        at_ = nullptr;
        return *this;
    }

    [[nodiscard]] friend constexpr bool operator==(OptionCursor const& left, OptionCursor const& right) noexcept {
        return left.at_ == right.at_;
    }
};

template <ChoicePayload T>
class [[nodiscard]] Option {
    detail::OptionSlot<T> slot_;

    template <class U>
    constexpr Option(detail::SomeTag tag, U&& value) noexcept(std::is_nothrow_constructible_v<T, U&&>)
        : slot_{tag, static_cast<U&&>(value)} {}

public:
    using value_type = T;

    constexpr Option() noexcept = default;
    constexpr Option(NoValue) noexcept {}

    template <class U>
        requires std::constructible_from<T, U&&>
    [[nodiscard]] static constexpr Option some(U&& value) noexcept(std::is_nothrow_constructible_v<T, U&&>) {
        return Option{detail::SomeTag{}, static_cast<U&&>(value)};
    }

    [[nodiscard]] constexpr bool is_some() const noexcept { return slot_.holds_(); }
    [[nodiscard]] constexpr bool is_none() const noexcept { return !slot_.holds_(); }

    // The fatal unwrap at a boundary.  The reason tells why the Option
    // holds a value there.  An empty Option gives the reason and the place
    // of the call to fatal(), and the process ends.  The Option is empty
    // after the call.  A passing expect() costs one branch.
    [[nodiscard]] constexpr T expect(Fmt<> why) && noexcept {
        if (!slot_.holds_()) [[unlikely]] {
            fatal(why);
        }
        T taken(static_cast<T&&>(slot_.payload_()));
        slot_.clear_();
        return taken;
    }

    // The payload, or fallback when the Option is empty.  The Option is
    // empty after the call.
    [[nodiscard]] constexpr T value_or(T fallback) && noexcept {
        if (!slot_.holds_()) return fallback;
        T taken(static_cast<T&&>(slot_.payload_()));
        slot_.clear_();
        return taken;
    }

    // The total match.  on_some gets the payload, or on_none runs, and
    // the two arms give the same type.  A call with one arm does not
    // compile, so no caller forgets the empty case.  The rvalue form moves
    // the payload out first, so the Option is empty when on_some runs.
    template <class OnSome, class OnNone>
        requires std::is_invocable_v<OnSome, T&&> && std::is_invocable_v<OnNone>
              && std::same_as<std::invoke_result_t<OnSome, T&&>, std::invoke_result_t<OnNone>>
    constexpr std::invoke_result_t<OnNone> match(OnSome&& on_some,
                                                 OnNone&& on_none) && noexcept(std::is_nothrow_invocable_v<OnSome, T&&>
                                                                               && std::is_nothrow_invocable_v<OnNone>) {
        if (!slot_.holds_()) return static_cast<OnNone&&>(on_none)();
        T taken(static_cast<T&&>(slot_.payload_()));
        slot_.clear_();
        return static_cast<OnSome&&>(on_some)(static_cast<T&&>(taken));
    }

    // The total match of a borrow: on_some gets the payload as a const
    // reference, and the Option keeps it.
    template <class OnSome, class OnNone>
        requires std::is_invocable_v<OnSome, T const&> && std::is_invocable_v<OnNone>
              && std::same_as<std::invoke_result_t<OnSome, T const&>, std::invoke_result_t<OnNone>>
    constexpr std::invoke_result_t<OnNone>
    match(OnSome&& on_some, OnNone&& on_none) const& noexcept(std::is_nothrow_invocable_v<OnSome, T const&>
                                                              && std::is_nothrow_invocable_v<OnNone>) {
        if (!slot_.holds_()) return static_cast<OnNone&&>(on_none)();
        return static_cast<OnSome&&>(on_some)(slot_.payload_());
    }

    // The loop `for (auto& value : option)` makes one turn when the Option
    // holds a value, and no turn when it is empty.  A cursor of a
    // temporary Option would point into an object that the full
    // expression ends, so the overloads for an rvalue are deleted.  A loop
    // over a temporary binds it to a reference first, and calls these
    // overloads on that lvalue.
    [[nodiscard]] constexpr OptionCursor<T> begin() & noexcept {
        return OptionCursor<T>{slot_.holds_() ? &slot_.payload_() : nullptr};
    }
    [[nodiscard]] constexpr OptionCursor<T> end() & noexcept { return OptionCursor<T>{nullptr}; }
    [[nodiscard]] constexpr OptionCursor<T const> begin() const& noexcept {
        return OptionCursor<T const>{slot_.holds_() ? &slot_.payload_() : nullptr};
    }
    [[nodiscard]] constexpr OptionCursor<T const> end() const& noexcept { return OptionCursor<T const>{nullptr}; }

    void begin() && = delete("a cursor of a temporary Option points into an object that the full expression ends");
    void end() && = delete("a cursor of a temporary Option points into an object that the full expression ends");
    void begin() const&& = delete("a cursor of a temporary Option points into an object that the full expression ends");
    void end() const&& = delete("a cursor of a temporary Option points into an object that the full expression ends");
};

}  // namespace foundation::core
