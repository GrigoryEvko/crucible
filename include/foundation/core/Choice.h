#pragma once

// The Choice family: a value that can be absent, and the result of an
// operation that can fail.
//
// Result<T, E> holds one T or one error E.  A partial operation of the
// base gives a Result, and its caller handles the error with the same
// roads as for an Option: match(), ok(), err(), value_or() and expect().
// CRUCIBLE_TRY takes the value, or returns the error to the caller of the
// function.
// An error is a value: a scoped enum, or a trivially copyable class of at
// most 16 bytes, so an error never allocates.  An error can also be a
// linear class, so a failed operation can give a resource back.  Err<E>
// marks an error, so `return err(Code::full);` and `return value;` both
// convert to the Result.  An Err<F> converts to a Result with the error E
// when F converts to E with no throw.  An Option and a Result do not convert
// to each other: ok() and ok_or() make the crossing.
//
// Option<T> holds one T or no value.  The only roads to the payload are a
// loop of zero or one turns, the total match(), value_or(), ok_or(), and
// expect(), the fatal unwrap with a reason.  The type has no operator* and no
// operator->, so a read of an empty Option is not possible.  The whole
// type is [[nodiscard]].
//
// The storage has three forms, and the payload type selects the form:
//
//   niche    The payload declares an empty value through niche<T>.  The
//            Option then has the size of the payload, and the empty value
//            is the state "no value".
//   plain    A trivially copyable payload with no niche.  The Option holds
//            the payload and a tag of one byte, in the layout of a plain
//            Result, and it is trivially copyable.  The x86-64 System V
//            ABI then gives it back in registers when it has at most 16
//            bytes.
//   owning   Each other payload.  The Option is move-only.  A move leaves
//            the source empty, and the destructor destroys the payload.
//
// A move of a niche or plain Option is a copy of the bytes, so the source
// keeps its value.  The payload owns nothing in those two forms, so two
// copies of it are safe.  expect(), value_or() and the rvalue match()
// empty the source in each form.
//
// A linear payload (foundation/Linear.h) takes the owning form, also when
// its bytes are trivially copyable, so a move never leaves a second copy
// of the value.  An Option or a Result gives a linear payload only through
// a consumer that takes the Option or the Result as an rvalue: expect(),
// ok(), err() and the rvalue match().  A read through a const reference
// consumes nothing.  value_or() and the cursor of a mutable loop do not
// exist for a linear payload: the first drops its fallback, and a move
// through the second empties the payload in place.
//
// The families of foundation/core use only the language, the compiler
// builtins and the headers of the base allowance.  fixy/Core.h puts their
// public names into namespace fixy.  A check of a family does the same
// work in each build and with each contract semantic, and it ends the
// process through fatal() of the Report family.

#include <foundation/ByteSeal.h>
#include <foundation/Linear.h>
#include <foundation/core/Report.h>
#include <foundation/reflect/Instance.h>

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

namespace detail {

// The annotation of the mark Err.  No other class of the family carries it.
struct ErrorMarkKind final {};

// An Err<E>.  An Err marks a return, and it is not a value, so the payload
// gate and the error gate refuse it.  The concept reads only the annotations
// of the class T, so a unit cannot change its answer for a different type.
template <class T>
concept ErrMark = std::is_class_v<T> && ::foundation::linear::carries_annotation(^^T, ^^ErrorMarkKind);

// The annotation of the primary template of niche.  A specialization does
// not carry it, so its absence tells that a type declares a niche.
struct UndeclaredNiche final {};

enum class OptionForm : unsigned char {
    niche,
    plain,
    owning,
};

// The storage of each Option, and the one reader of the niche of T.
template <class T, OptionForm Form>
class OptionSlot;

template <class T>
class NicheRead;

}  // namespace detail

// The key of the niche protocol.  The empty value of a niche is a value that
// no valid T holds, such as a Ref that holds the null pointer, so only the
// storage of an Option reads it.  Each member of the protocol takes the key,
// and only detail::NicheRead<T> builds a niche_key<T>.  A key opens the niche
// of its own payload type only.  So the niche of a type gets a key that
// cannot read the niche of a different type.
//
// The protocol takes the key by reference, so nothing copies one.  Both
// constructors are user-provided and private, so std::bit_cast and a
// lifetime start over bytes build no key, for the reasons that grade_key of
// foundation/algebra/Graded.h gives.
template <class T>
class [[nodiscard]] niche_key final {
    friend class detail::NicheRead<T>;

    constexpr niche_key() noexcept {}
    constexpr niche_key(niche_key const&) noexcept {}

public:
    niche_key& operator=(niche_key const&) = delete("a niche key lives for one call of the niche protocol");
};

// The niche protocol.  A payload type T with an empty value that no valid
// T holds can specialize niche<T> with two static members:
//
//   static constexpr T empty(niche_key<T> const&) noexcept;    the empty value
//   static constexpr bool is_empty(niche_key<T> const&, T const&) noexcept;
//
// A class can also give the two members itself, which a class in a
// namespace that cannot specialize niche needs:
//
//   static constexpr T niche_empty(niche_key<T> const&) noexcept;
//   constexpr bool is_niche_empty(niche_key<T> const&) const noexcept;
//
// The members must be public.  The key is the proof that the storage of an
// Option calls them, so no other code gets the empty value.  The primary
// template declares no niche, so a type with no specialization and no
// member of the protocol has no niche.  A niche payload has trivial copy and
// move constructors and a trivial destructor, so the ABI passes the Option
// in registers.  Its assignment can be user-provided, as the assignment of a
// type that holds a byte seal is.
template <class T>
struct[[= detail::UndeclaredNiche{}]] niche {};

namespace detail {

// True when niche<T> is the primary template, so T declares no niche.
template <class T>
concept UndeclaredNicheOf = ::foundation::linear::carries_annotation(^^niche<T>, ^^UndeclaredNiche);

// A class that names a member of the member protocol.
template <class T>
concept NamesMemberNiche = requires { &T::niche_empty; } || requires { &T::is_niche_empty; };

}  // namespace detail

// The niche of a class that gives the members of the protocol.  A class
// that gives only one of the two members, or a member of the wrong type,
// declares a niche that is not well formed, and the payload gate refuses it.
template <class T>
    requires detail::NamesMemberNiche<T>
struct niche<T> {
    [[nodiscard]] static constexpr T empty(niche_key<T> const& key) noexcept
        requires requires(niche_key<T> const& held_key) {
            { T::niche_empty(held_key) } noexcept -> std::same_as<T>;
        }
    {
        return T::niche_empty(key);
    }

    [[nodiscard]] static constexpr bool is_empty(niche_key<T> const& key, T const& value) noexcept
        requires requires(niche_key<T> const& held_key, T const& held) {
            { held.is_niche_empty(held_key) } noexcept -> std::same_as<bool>;
        }
    {
        return value.is_niche_empty(key);
    }
};

namespace detail {

// The one reader of the niche of T, and the one class that builds a
// niche_key<T>.  The storage of a niche Option reads the empty value
// through its private members.  The gate and the lifetime mark read only
// its two facts, which are constants of type bool, so no other code gets an
// empty value or a key.
template <class T>
class NicheRead final {
    friend class OptionSlot<T, OptionForm::niche>;

    [[nodiscard]] static constexpr T empty_() noexcept { return niche<T>::empty(niche_key<T>{}); }
    [[nodiscard]] static constexpr bool is_empty_(T const& value) noexcept {
        return niche<T>::is_empty(niche_key<T>{}, value);
    }

    // True when each byte of the empty value is zero.  A constant
    // evaluation reads the bytes.  A payload that is not trivially copyable
    // gives false.
    [[nodiscard]] static consteval bool empty_bytes_are_zero_() noexcept {
        if constexpr (!std::is_trivially_copyable_v<T>) {
            return false;
        } else {
            struct Image {
                unsigned char byte[sizeof(T)];
            };
            Image const image = __builtin_bit_cast(Image, empty_());
            for (unsigned char const byte : image.byte) {
                if (byte != 0) return false;
            }
            return true;
        }
    }

public:
    // True when is_empty gives true for the empty value.
    [[nodiscard]] static consteval bool holds_its_empty() noexcept { return is_empty_(empty_()); }

    // True when each byte of the empty value is zero, and false when a
    // constant evaluation cannot read the bytes, such as the bytes of a
    // pointer.  So the answer fails closed.
    [[nodiscard]] static consteval bool empty_has_zero_bytes() noexcept {
        if constexpr (requires { typename std::bool_constant<empty_bytes_are_zero_()>; }) {
            return empty_bytes_are_zero_();
        } else {
            return false;
        }
    }
};

}  // namespace detail

// The type declares no niche, or its niche is well formed.  A type declares
// a niche when niche<T> is a specialization.  A well formed niche has the
// trivial copy, move and destructor of the protocol, and its two members
// take the key, have the types of the protocol and do not throw.  Its empty
// value is a constant, and is_empty gives true for it.  A malformed niche
// would give the plain or the owning form with no message, so the payload
// gate refuses it.  This gate is the one gate that reads niche<T>.
template <class T>
concept WellFormedNiche = detail::UndeclaredNicheOf<T>
                       || (std::is_trivially_copy_constructible_v<T> && std::is_trivially_move_constructible_v<T>
                           && std::is_trivially_destructible_v<T> && std::is_nothrow_copy_assignable_v<T>
                           && requires(T const& value, niche_key<T> const& key) {
                                  { niche<T>::empty(key) } noexcept -> std::same_as<T>;
                                  { niche<T>::is_empty(key, value) } noexcept -> std::same_as<bool>;
                                  typename std::bool_constant<detail::NicheRead<T>::holds_its_empty()>;
                              } && detail::NicheRead<T>::holds_its_empty());

// The payload of an Option: an object type that is not an array, not
// cv-qualified, not the empty marker itself and not the mark of an error.
// The move and the destructor do not throw, so a move never leaves a
// half-moved Option.  A niche that the type declares is well formed.
template <class T>
concept ChoicePayload = std::is_object_v<T> && !std::is_array_v<T> && !std::is_const_v<T> && !std::is_volatile_v<T>
                     && !std::same_as<T, NoValue> && !detail::ErrMark<T> && std::is_nothrow_move_constructible_v<T>
                     && std::is_nothrow_destructible_v<T> && WellFormedNiche<T>;

// A payload that a member can drop or copy.  A linear payload is not one,
// because its loss or its copy changes what the program can prove.  A
// linear class never copies, so a payload that copies is droppable, and
// the walk of the marker runs only for a payload that only moves.  That
// walk reads the members of the payload.
template <class T>
concept DroppablePayload = std::is_copy_constructible_v<T> || !::foundation::linear::is_linear_type(^^T);

// A payload whose bytes an Option or a Result can copy: trivially copyable,
// trivially destructible and droppable.  The conjunction stops at its
// first false part, so the walk of DroppablePayload does not run for a
// payload that a part before it refuses.
template <class T>
concept PlainPayload = std::is_trivially_copyable_v<T> && std::is_trivially_destructible_v<T> && DroppablePayload<T>;

// The value of a Result of an operation that gives no value.
struct Unit final {
    [[nodiscard]] friend constexpr bool operator==(Unit, Unit) noexcept = default;
};

// An error: a scoped enum, a trivially copyable and assignable class of at
// most 16 bytes, or a linear class.  A plain integer is refused, because
// its meaning is not in its type.  A copyable class that owns memory is
// refused, because an error never allocates.  A class with a reference
// member is refused, because the reference cannot be assigned and can
// outlive its object.  A linear error gives a resource back to the caller
// of a failed operation.  It moves and ends with no throw, and only a
// consumer of the Result gives it out.  An Err marks a return, so it is
// not an error value itself.
template <class E>
concept ErrorValue = !std::is_const_v<E> && !std::is_volatile_v<E> && !detail::ErrMark<E>
                  && (std::is_scoped_enum_v<E>
                      || (std::is_class_v<E> && std::is_trivially_copyable_v<E> && std::is_trivially_destructible_v<E>
                          && std::is_trivially_copy_assignable_v<E> && sizeof(E) <= 16)
                      || (::foundation::linear::is_linear_type(^^E)
                          && std::is_nothrow_move_constructible_v<E> && std::is_nothrow_destructible_v<E>));

// An error F that a Result with the error E takes from an Err<F>: a
// different error type, with an implicit conversion to E that does not
// throw.  An explicit conversion is refused, because the return of an Err
// shows no conversion.
template <class F, class E>
concept ErrorConvertsTo = ErrorValue<F> && ErrorValue<E> && !std::same_as<F, E> && std::is_nothrow_convertible_v<F, E>;

template <ChoicePayload T>
class Option;

template <ChoicePayload T, ErrorValue E>
class Result;

// The mark of an error, so that an error and a value of the same type
// never meet in one conversion.  Only a Result reads the error of the mark.
template <ErrorValue E>
class[[= detail::ErrorMarkKind{}]] Err final {
    E error_;

    template <ChoicePayload T, ErrorValue F>
    friend class Result;

public:
    constexpr explicit Err(E error) noexcept : error_{static_cast<E&&>(error)} {}
};

[[nodiscard]] constexpr auto err(ErrorValue auto error) noexcept {
    return Err<decltype(error)>{static_cast<decltype(error)&&>(error)};
}

namespace detail {

struct NoPayload final {};

// The dispatch tag of the constructor that builds a full Option.
struct SomeTag final {
    explicit constexpr SomeTag() noexcept = default;
};

// A payload that declares a niche takes the niche form.  The payload gate
// holds WellFormedNiche<T>, so its niche is well formed.  A niche payload
// copies, so it is never linear.  A linear payload takes the owning form
// also when its bytes are trivially copyable.  Each branch that the form
// does not take stays out of the instantiation, so a payload with a niche
// is never walked: a member of such a payload can name its Option, which is
// not complete while its form is chosen.
template <class T>
consteval OptionForm option_form_for() noexcept {
    if constexpr (!UndeclaredNicheOf<T>) {
        return OptionForm::niche;
    } else if constexpr (PlainPayload<T>) {
        return OptionForm::plain;
    } else {
        return OptionForm::owning;
    }
}

template <class T>
inline constexpr OptionForm option_form_of = option_form_for<T>();

// The mark of a niche Option against a lifetime start over bytes.  Zero
// bytes are the empty value of a niche whose empty value has zero bytes.
// For each other niche, zero bytes are a full Option with a value that no
// constructor made, so the mark carries the annotation no_start_over_bytes,
// and the checked lifetime start of foundation/Lifetime.h refuses the
// Option.  The mark is an empty base of the slot, and not a member, so the
// Option keeps the size of its payload.  GCC 16 gives no unique object
// representations to a class with an empty member, and an empty base keeps
// them, so an Atomic cell can hold an Option of a niche payload.
template <bool IsZeroBytes>
struct NicheLifetime;

template <>
struct NicheLifetime<true> {};

template <>
struct[[= ::foundation::lifetime::no_start_over_bytes{}]] NicheLifetime<false> {};

// The storage of each Option.  Each slot keeps its parts private, and only
// the Option that holds the slot reads them.
template <class T, OptionForm Form = option_form_of<T>>
class OptionSlot;

template <class T>
class OptionSlot<T, OptionForm::niche> : NicheLifetime<NicheRead<T>::empty_has_zero_bytes()> {
    T value_ = NicheRead<T>::empty_();

    template <ChoicePayload U>
    friend class ::foundation::core::Option;

    [[nodiscard]] constexpr bool holds_() const noexcept { return !NicheRead<T>::is_empty_(value_); }
    [[nodiscard]] constexpr T& payload_() noexcept { return value_; }
    [[nodiscard]] constexpr T const& payload_() const noexcept { return value_; }
    constexpr void clear_() noexcept { value_ = NicheRead<T>::empty_(); }

public:
    constexpr OptionSlot() noexcept = default;

    template <class U>
    constexpr OptionSlot(SomeTag, U&& value) noexcept(std::is_nothrow_constructible_v<T, U&&>)
        : value_(static_cast<U&&>(value)) {
        // A payload equal to the empty value would read as no value.  A
        // payload that cannot hold the empty value, such as a View that its
        // door bounds, gives the optimizer what it needs to remove the check.
        if (NicheRead<T>::is_empty_(value_)) [[unlikely]] {
            fatal("Option::some got the empty value of the niche of its payload");
        }
    }
};

template <bool IsPlain, class... Ts>
class SumSlotOf;

// The one layout rule of the plain forms of the family: the storage, and
// then a tag of one byte, as in std::expected and std::variant.  The x86-64
// System V ABI gives a class of at most 16 bytes back in two registers.  For
// a storage of four bytes, the storage is the low word of the first
// register, so a value goes in and out with no shift, and a test of the tag
// at bit 32 takes one shift.  The plain Option, and the sums whose
// alternatives are all plain, use this rule.  Only the slot of a plain sum
// reads the two cells.
template <class Storage, class Tag>
class TaggedCells {
    static_assert(sizeof(Tag) == 1, "the tag of the cells is one byte");

    Storage storage_;
    Tag tag_;

    template <bool IsPlain, class... Ts>
    friend class SumSlotOf;

public:
    template <class... Args>
    constexpr explicit TaggedCells(Tag tag,
                                   Args&&... args) noexcept(std::is_nothrow_constructible_v<Storage, Args&&...>)
        : storage_(static_cast<Args&&>(args)...), tag_{tag} {}
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
    constexpr Option() noexcept = default;
    constexpr Option(NoValue) noexcept {}

    template <class F>
    Option(Err<F>) = delete("a Result carries an error that an Option drops: write std::move(result).ok()");

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
    // empty after the call.  A full Option drops the fallback, so a linear
    // payload has no value_or().
    [[nodiscard]] constexpr T value_or(T fallback) && noexcept
        requires DroppablePayload<T>
    {
        if (!slot_.holds_()) return fallback;
        T taken(static_cast<T&&>(slot_.payload_()));
        slot_.clear_();
        return taken;
    }

    // The payload as a Result, or the error when the Option is empty.  The
    // Option is empty after the call.  A full Option drops the error, so a
    // linear error has no ok_or().  The call costs one branch.
    template <class E>
        requires ErrorValue<E> && DroppablePayload<E>
    [[nodiscard]] constexpr Result<T, E> ok_or(E error) && noexcept {
        if (!slot_.holds_()) return Result<T, E>{Err<E>{static_cast<E&&>(error)}};
        T taken(static_cast<T&&>(slot_.payload_()));
        slot_.clear_();
        return Result<T, E>{static_cast<T&&>(taken)};
    }

    // The total match.  on_some gets the payload, or on_none runs, and
    // the two arms give the same type.  A call with one arm does not
    // compile, so no caller forgets the empty case.  The rvalue form moves
    // the payload out first, so the Option is empty when on_some runs.  The
    // payload then lives in the match only, so an arm that gives a
    // reference is refused: the reference would dangle.
    template <class OnSome, class OnNone>
        requires std::is_invocable_v<OnSome, T&&> && std::is_invocable_v<OnNone>
              && std::same_as<std::invoke_result_t<OnSome, T&&>, std::invoke_result_t<OnNone>>
              && (!std::is_reference_v<std::invoke_result_t<OnNone>>)
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
    // overloads on that lvalue.  A linear payload has only the const
    // cursor, so no loop moves the payload out and leaves the Option full.
    [[nodiscard]] constexpr OptionCursor<T> begin() & noexcept
        requires DroppablePayload<T>
    {
        return OptionCursor<T>{slot_.holds_() ? &slot_.payload_() : nullptr};
    }
    [[nodiscard]] constexpr OptionCursor<T> end() & noexcept
        requires DroppablePayload<T>
    {
        return OptionCursor<T>{nullptr};
    }
    [[nodiscard]] constexpr OptionCursor<T const> begin() const& noexcept {
        return OptionCursor<T const>{slot_.holds_() ? &slot_.payload_() : nullptr};
    }
    [[nodiscard]] constexpr OptionCursor<T const> end() const& noexcept { return OptionCursor<T const>{nullptr}; }

    void begin() && = delete("a cursor of a temporary Option points into an object that the full expression ends");
    void end() && = delete("a cursor of a temporary Option points into an object that the full expression ends");
    void begin() const&& = delete("a cursor of a temporary Option points into an object that the full expression ends");
    void end() const&& = delete("a cursor of a temporary Option points into an object that the full expression ends");
};

// A full Option of the type of the value, with no conversion:
// `return some(slot);` gives an Option<SlotId> for a SlotId.
template <class V>
    requires ChoicePayload<std::remove_cvref_t<V>> && std::constructible_from<std::remove_cvref_t<V>, V&&>
[[nodiscard]] constexpr Option<std::remove_cvref_t<V>>
some(V&& value) noexcept(std::is_nothrow_constructible_v<std::remove_cvref_t<V>, V&&>) {
    return Option<std::remove_cvref_t<V>>::some(static_cast<V&&>(value));
}

namespace detail {

// The dispatch tag of the constructor of the alternative at an index.
template <std::size_t Index>
struct AlternativeAt final {
    explicit constexpr AlternativeAt() noexcept = default;
};

// The dispatch tag of the constructor that moves the active alternative of
// a different storage.
struct MoveActive final {
    explicit constexpr MoveActive() noexcept = default;
};

// The storage of a sum: one alternative alive at a time.  The union nests
// the alternatives after the first one, so each alternative is a direct
// member of the union at its depth, and a constant evaluation can follow the
// active member.  The destructor is trivial when each alternative is
// trivially destructible.  Else it does nothing, and the slot destroys the
// active alternative.  Complexity: the depth of the nest, and each walk of
// an index, are linear in the number of alternatives.  The alternatives are
// private, and only alternative_of_ reads one.
template <class... Ts>
union Alternatives;

// The alternative at an index of a storage.
template <std::size_t Index, class Storage>
[[nodiscard]] constexpr auto& alternative_of_(Storage& storage) noexcept;

template <class Last>
union Alternatives<Last> {
private:
    Last head_;

    template <std::size_t Index, class Storage>
    friend constexpr auto& alternative_of_(Storage& storage) noexcept;

public:
    template <class U>
    constexpr Alternatives(AlternativeAt<0>, U&& value) noexcept(std::is_nothrow_constructible_v<Last, U&&>)
        : head_(static_cast<U&&>(value)) {}

    // No member is active until the body builds the alternative of the
    // index, so the active member of the source tells which member to build.
    constexpr Alternatives(MoveActive, std::size_t, Alternatives& source) noexcept {
        ::new(static_cast<void*>(&head_)) Last(static_cast<Last&&>(source.head_));
    }

    constexpr ~Alternatives()
        requires std::is_trivially_destructible_v<Last>
    = default;
    constexpr ~Alternatives() {}

    constexpr void destroy_(std::size_t) noexcept { head_.~Last(); }
};

template <class Head, class Next, class... Rest>
union Alternatives<Head, Next, Rest...> {
private:
    Head head_;
    Alternatives<Next, Rest...> tail_;

    template <std::size_t Index, class Storage>
    friend constexpr auto& alternative_of_(Storage& storage) noexcept;

public:
    template <class U>
    constexpr Alternatives(AlternativeAt<0>, U&& value) noexcept(std::is_nothrow_constructible_v<Head, U&&>)
        : head_(static_cast<U&&>(value)) {}

    template <std::size_t Index, class U>
        requires(Index > 0)
    constexpr Alternatives(AlternativeAt<Index>, U&& value) noexcept(
        std::is_nothrow_constructible_v<Alternatives<Next, Rest...>, AlternativeAt<Index - 1>, U&&>)
        : tail_(AlternativeAt<Index - 1>{}, static_cast<U&&>(value)) {}

    constexpr Alternatives(MoveActive, std::size_t index, Alternatives& source) noexcept {
        if (index == 0) {
            ::new(static_cast<void*>(&head_)) Head(static_cast<Head&&>(source.head_));
        } else {
            ::new(static_cast<void*>(&tail_)) Alternatives<Next, Rest...>(MoveActive{}, index - 1, source.tail_);
        }
    }

    constexpr ~Alternatives()
        requires(std::is_trivially_destructible_v<Head>
                 && std::is_trivially_destructible_v<Alternatives<Next, Rest...>>)
    = default;
    constexpr ~Alternatives() {}

    constexpr void destroy_(std::size_t index) noexcept {
        if (index == 0) {
            head_.~Head();
        } else {
            tail_.destroy_(index - 1);
        }
    }
};

template <std::size_t Index, class Storage>
[[nodiscard]] constexpr auto& alternative_of_(Storage& storage) noexcept {
    if constexpr (Index == 0) {
        return storage.head_;
    } else {
        return alternative_of_<Index - 1>(storage.tail_);
    }
}

// A sum is plain when each alternative is plain.  A linear alternative takes
// the owning slot, also when its bytes are trivially copyable, so a move
// never leaves a second copy of it.
template <class... Ts>
inline constexpr bool is_plain_sum_v = (PlainPayload<Ts> && ...);

// The unsigned word of a width in bytes.
template <std::size_t Width>
struct WordOf;

template <>
struct WordOf<1> {
    using type = unsigned char;
};

template <>
struct WordOf<2> {
    using type = unsigned short;
};

template <>
struct WordOf<4> {
    using type = unsigned int;
};

template <>
struct WordOf<8> {
    using type = unsigned long long;
};

// Count zero words.  The words are members and not an array, because
// -fstack-protector-strong puts a canary on each frame that holds an array,
// and a sum with a canary stays in memory and not in registers.  The words
// make a balanced tree of two halves, so the depth of the instantiation is
// the binary logarithm of Count, and a large storage stays below the
// template depth limit of the compiler.  Complexity: at most two
// instantiations for each level of the tree, O(log Count).
template <class Word, std::size_t Count>
struct ZeroWords {
    ZeroWords<Word, Count / 2> low_{};
    ZeroWords<Word, Count - Count / 2> high_{};
};

template <class Word>
struct ZeroWords<Word, 1> {
    Word first_ = 0;
};

// An alternative of a plain sum.  An empty alternative, such as the empty
// marker of an Option or the Unit of a Result, has no value bytes.  When
// WritesZeroWords is true, it writes zero words over the full storage, so a
// copy of the sum reads no byte that no constructor wrote (SumSlotOf tells
// when).  An alternative that holds a value leaves the bytes after it as they
// are, as std::expected does, so its value stays in a register.  Width is the
// alignment of the storage, at most eight bytes.  Only the slot of a plain
// sum reads the value.
template <class T, std::size_t Size, std::size_t Width, bool WritesZeroWords>
class PlainAlternative {
    T value_;

    template <bool IsPlain, class... Ts>
    friend class SumSlotOf;

public:
    template <class U>
    constexpr explicit PlainAlternative(U&& from) noexcept(std::is_nothrow_constructible_v<T, U&&>)
        : value_(static_cast<U&&>(from)) {}
};

template <class T, std::size_t Size, std::size_t Width>
class PlainAlternative<T, Size, Width, true> {
    [[no_unique_address]] T value_;
    ZeroWords<typename WordOf<Width>::type, Size / Width> zero_{};

    template <bool IsPlain, class... Ts>
    friend class SumSlotOf;

public:
    template <class U>
    constexpr explicit PlainAlternative(U&& from) noexcept(std::is_nothrow_constructible_v<T, U&&>)
        : value_(static_cast<U&&>(from)) {}
};

template <class T, class E>
class ResultSlot;

// The slot of a plain sum: the storage and the tag of TaggedCells.  The
// special members are implicit, and each one is trivial, because each
// alternative is trivially copyable.  The tag is the index of the active
// alternative.  The storage keeps the size and the alignment of the union of
// the bare alternatives.
//
// The ABI of x86_64 and of aarch64 gives a sum of at most 16 bytes back in
// two registers.  GCC can then read each byte of the storage into a register,
// also when the active alternative is empty.  -Wmaybe-uninitialized then
// reports a byte that no constructor wrote.  So the empty alternative of such
// a sum writes zero words over the storage (PlainAlternative).  A larger sum
// goes through memory, and GCC gives no such report for it.  Its empty
// alternative writes no byte, so a clear of an Option of a large payload
// writes only its tag, as the reset of std::optional does.
template <class... Ts>
class SumSlotOf<true, Ts...> {
    static constexpr std::size_t storage_size_ = sizeof(Alternatives<Ts...>);
    static constexpr std::size_t word_width_ = alignof(Alternatives<Ts...>) < 8 ? alignof(Alternatives<Ts...>) : 8;
    static constexpr bool is_register_pair_ = sizeof(TaggedCells<Alternatives<Ts...>, unsigned char>) <= 16;
    using Storage =
        Alternatives<PlainAlternative<Ts, storage_size_, word_width_, std::is_empty_v<Ts> && is_register_pair_>...>;
    static_assert(sizeof(Storage) == storage_size_ && alignof(Storage) == alignof(Alternatives<Ts...>),
                  "the zero words of an empty alternative do not change the storage");

    TaggedCells<Storage, unsigned char> cells_;

    template <class T, OptionForm Form>
    friend class OptionSlot;
    template <class T, class E>
    friend class ResultSlot;

    [[nodiscard]] constexpr std::size_t index_() const noexcept { return cells_.tag_; }

    template <std::size_t Index>
    [[nodiscard]] constexpr auto& alternative_() noexcept {
        return alternative_of_<Index>(cells_.storage_).value_;
    }
    template <std::size_t Index>
    [[nodiscard]] constexpr auto const& alternative_() const noexcept {
        return alternative_of_<Index>(cells_.storage_).value_;
    }

public:
    template <std::size_t Index, class U>
    constexpr SumSlotOf(AlternativeAt<Index> at,
                        U&& value) noexcept(std::is_nothrow_constructible_v<Storage, AlternativeAt<Index>, U&&>)
        : cells_{static_cast<unsigned char>(Index), at, static_cast<U&&>(value)} {}
};

// The slot of a sum with an alternative that owns something.  The slot is
// move-only.  A move builds the active alternative from the active
// alternative of the source, and the source keeps its moved-from
// alternative, which its destructor ends.  A borrow of the source then
// stays a borrow of a live object.
template <class... Ts>
class SumSlotOf<false, Ts...> {
    Alternatives<Ts...> storage_;
    unsigned char tag_;

    template <class T, OptionForm Form>
    friend class OptionSlot;
    template <class T, class E>
    friend class ResultSlot;

    [[nodiscard]] constexpr std::size_t index_() const noexcept { return tag_; }

    template <std::size_t Index>
    [[nodiscard]] constexpr auto& alternative_() noexcept {
        return alternative_of_<Index>(storage_);
    }
    template <std::size_t Index>
    [[nodiscard]] constexpr auto const& alternative_() const noexcept {
        return alternative_of_<Index>(storage_);
    }

public:
    template <std::size_t Index, class U>
    constexpr SumSlotOf(AlternativeAt<Index> at, U&& value) noexcept(
        std::is_nothrow_constructible_v<Alternatives<Ts...>, AlternativeAt<Index>, U&&>)
        : storage_(at, static_cast<U&&>(value)), tag_{static_cast<unsigned char>(Index)} {}

    constexpr SumSlotOf(SumSlotOf&& other) noexcept
        : storage_(MoveActive{}, other.tag_, other.storage_), tag_{other.tag_} {}

    SumSlotOf(SumSlotOf const&) = delete("an owning sum is move-only: a copy would duplicate what an alternative owns");
    SumSlotOf& operator=(SumSlotOf const&) = delete("an owning sum is move-only");
    SumSlotOf& operator=(SumSlotOf&&) = delete("an owning sum is not assignable: build a new sum instead");

    constexpr ~SumSlotOf() { storage_.destroy_(tag_); }
};

// The one slot of the sums of the family.
template <class... Ts>
using SumSlot = SumSlotOf<is_plain_sum_v<Ts...>, Ts...>;

// A plain Option is the plain sum of the empty marker and the payload.  The
// tag is 0 for no value and 1 for a value, as the tag of a Result is.  The
// special members are implicit and trivial.
template <class T>
class OptionSlot<T, OptionForm::plain> {
    SumSlot<NoPayload, T> sum_{AlternativeAt<0>{}, NoPayload{}};

    template <ChoicePayload U>
    friend class ::foundation::core::Option;

    [[nodiscard]] constexpr bool holds_() const noexcept { return sum_.index_() != 0; }
    [[nodiscard]] constexpr T& payload_() noexcept { return sum_.template alternative_<1>(); }
    [[nodiscard]] constexpr T const& payload_() const noexcept { return sum_.template alternative_<1>(); }
    constexpr void clear_() noexcept { sum_ = SumSlot<NoPayload, T>{AlternativeAt<0>{}, NoPayload{}}; }

public:
    constexpr OptionSlot() noexcept = default;

    template <class U>
    constexpr OptionSlot(SomeTag, U&& value) noexcept(std::is_nothrow_constructible_v<T, U&&>)
        : sum_{AlternativeAt<1>{}, static_cast<U&&>(value)} {}
};

// An owning Option holds the alternatives of the empty marker and the
// payload, and a flag.  A move builds the payload in the new slot, and the
// source becomes empty.
template <class T>
class OptionSlot<T, OptionForm::owning> {
    Alternatives<NoPayload, T> storage_{AlternativeAt<0>{}, NoPayload{}};
    bool is_engaged_ = false;

    template <ChoicePayload U>
    friend class ::foundation::core::Option;

    [[nodiscard]] constexpr bool holds_() const noexcept { return is_engaged_; }
    [[nodiscard]] constexpr T& payload_() noexcept { return alternative_of_<1>(storage_); }
    [[nodiscard]] constexpr T const& payload_() const noexcept { return alternative_of_<1>(storage_); }
    constexpr void clear_() noexcept {
        if (is_engaged_) {
            storage_.destroy_(1);
            is_engaged_ = false;
        }
    }

public:
    constexpr OptionSlot() noexcept = default;

    template <class U>
    constexpr OptionSlot(SomeTag, U&& value) noexcept(std::is_nothrow_constructible_v<T, U&&>)
        : storage_(AlternativeAt<1>{}, static_cast<U&&>(value)), is_engaged_{true} {}

    constexpr OptionSlot(OptionSlot&& other) noexcept
        : storage_(MoveActive{}, other.is_engaged_ ? std::size_t{1} : std::size_t{0}, other.storage_),
          is_engaged_{other.is_engaged_} {
        other.clear_();
    }

    OptionSlot(OptionSlot const&) =
        delete("an owning Option is move-only: a copy would duplicate what the payload owns");
    OptionSlot& operator=(OptionSlot const&) = delete("an owning Option is move-only");
    OptionSlot& operator=(OptionSlot&&) = delete("an owning Option is not assignable: build a new Option instead");

    constexpr ~OptionSlot() {
        if (is_engaged_) storage_.destroy_(1);
    }
};

// The slot of a Result: SumSlot<E, T>, with the error at index 0 and the
// value at index 1.  A tag that is not zero then tells that a value is
// there, as the tag of a plain Option does, so a full Option and a Result
// of a value have one tag, and a test of the tag reads the same in each.
// The special members follow the sum.
template <class T, class E>
class ResultSlot {
    SumSlot<E, T> sum_;

    template <ChoicePayload U, ErrorValue F>
    friend class ::foundation::core::Result;

    [[nodiscard]] constexpr bool holds_() const noexcept { return sum_.index_() != 0; }
    [[nodiscard]] constexpr T& value_() noexcept { return sum_.template alternative_<1>(); }
    [[nodiscard]] constexpr T const& value_() const noexcept { return sum_.template alternative_<1>(); }
    [[nodiscard]] constexpr E& error_() noexcept { return sum_.template alternative_<0>(); }
    [[nodiscard]] constexpr E const& error_() const noexcept { return sum_.template alternative_<0>(); }

public:
    template <std::size_t Index, class U>
    constexpr ResultSlot(AlternativeAt<Index> at,
                         U&& part) noexcept(std::is_nothrow_constructible_v<SumSlot<E, T>, AlternativeAt<Index>, U&&>)
        : sum_{at, static_cast<U&&>(part)} {}
};

}  // namespace detail

// One value or one error.  The constructors take a value or an Err, so
// `return value;` and `return err(code);` give a Result.  A Result of a
// value that owns something gives the value out once: after expect(),
// value_or(), ok() or the rvalue match(), the Result holds the moved-from
// value, and the use-after-move guard refuses a second use.  The payload
// gate refuses each Err, so a value and an Err never meet in one
// conversion.  Zero bytes are a value or an error that no constructor made,
// so the annotation no_start_over_bytes refuses a lifetime start over bytes.
template <ChoicePayload T, ErrorValue E>
class [[nodiscard]][[= ::foundation::lifetime::no_start_over_bytes{}]] Result {
    detail::ResultSlot<T, E> slot_;

    // The return is a copy-initialization, so only an implicit conversion
    // builds the error.
    template <class F>
    [[nodiscard]] static constexpr E converted_error_(F&& error) noexcept {
        return static_cast<F&&>(error);
    }

public:
    constexpr Result(T value) noexcept : slot_{detail::AlternativeAt<1>{}, static_cast<T&&>(value)} {}
    constexpr Result(Err<E> error) noexcept : slot_{detail::AlternativeAt<0>{}, static_cast<E&&>(error.error_)} {}

    // The error of a callee converts to the error of the caller, so
    // `return err(net_error);` and a propagated error need no match.  The
    // conversion is implicit and does not throw.
    template <ErrorValue F>
        requires ErrorConvertsTo<F, E>
    constexpr Result(Err<F> error) noexcept
        : slot_{detail::AlternativeAt<0>{}, converted_error_<F>(static_cast<F&&>(error.error_))} {}

    template <class F>
    Result(Err<F>) = delete(
        "the error type does not convert to the error type of this Result: give E a noexcept implicit constructor "
        "from F, or map the error with match");

    Result(NoValue) = delete("an empty Option has no error for a Result: write std::move(option).ok_or(error)");

    [[nodiscard]] constexpr bool is_ok() const noexcept { return slot_.holds_(); }
    [[nodiscard]] constexpr bool is_err() const noexcept { return !slot_.holds_(); }

    // The value as an Option, which is empty for an error.
    [[nodiscard]] constexpr Option<T> ok() && noexcept {
        if (!slot_.holds_()) return Option<T>{none};
        return Option<T>::some(static_cast<T&&>(slot_.value_()));
    }

    // The error as an Option, which is empty for a value.  A copy of the
    // error is the Option of a droppable error.
    [[nodiscard]] constexpr Option<E> err() const& noexcept
        requires DroppablePayload<E>
    {
        if (slot_.holds_()) return Option<E>{none};
        return Option<E>::some(E{slot_.error_()});
    }

    // A linear error moves into the Option, so the caller gets the
    // resource back once.
    [[nodiscard]] constexpr Option<E> err() && noexcept
        requires(!DroppablePayload<E>)
    {
        if (slot_.holds_()) return Option<E>{none};
        return Option<E>::some(static_cast<E&&>(slot_.error_()));
    }

    // The value, or fallback for an error.  The call drops the fallback or
    // the error, so a linear value or error has no value_or().
    [[nodiscard]] constexpr T value_or(T fallback) && noexcept
        requires(DroppablePayload<T> && DroppablePayload<E>)
    {
        if (!slot_.holds_()) return fallback;
        return static_cast<T&&>(slot_.value_());
    }

    // The fatal unwrap at a boundary.  The reason tells why the operation
    // cannot fail there.  An error gives the reason and the place of the
    // call to fatal(), and the process ends.  A Result of Unit gives no
    // value, so its expect() gives void and the caller discards nothing.
    [[nodiscard]] constexpr T expect(Fmt<> why) && noexcept
        requires(!std::same_as<T, Unit>)
    {
        if (!slot_.holds_()) [[unlikely]] {
            fatal(why);
        }
        return static_cast<T&&>(slot_.value_());
    }

    constexpr void expect(Fmt<> why) && noexcept
        requires std::same_as<T, Unit>
    {
        if (!slot_.holds_()) [[unlikely]] {
            fatal(why);
        }
    }

    // The total match: on_ok gets the value, or on_err gets the error, and
    // the two arms give the same type.  A call with one arm does not
    // compile, so no caller forgets the error.  The rvalue form gives up
    // the Result, which is often a temporary, so an arm that gives a
    // reference is refused: the reference would dangle.  The error goes to
    // on_err as an rvalue, so a linear error moves to the arm.
    template <class OnOk, class OnErr>
        requires std::is_invocable_v<OnOk, T&&> && std::is_invocable_v<OnErr, E>
              && std::same_as<std::invoke_result_t<OnOk, T&&>, std::invoke_result_t<OnErr, E>>
              && (!std::is_reference_v<std::invoke_result_t<OnErr, E>>)
    constexpr std::invoke_result_t<OnErr, E>
    match(OnOk&& on_ok,
          OnErr&& on_err) && noexcept(std::is_nothrow_invocable_v<OnOk, T&&> && std::is_nothrow_invocable_v<OnErr, E>) {
        if (!slot_.holds_()) return static_cast<OnErr&&>(on_err)(static_cast<E&&>(slot_.error_()));
        return static_cast<OnOk&&>(on_ok)(static_cast<T&&>(slot_.value_()));
    }

    // The total match of a borrow: on_ok gets the value and on_err gets the
    // error, each as a const reference, and the Result keeps them.
    template <class OnOk, class OnErr>
        requires std::is_invocable_v<OnOk, T const&> && std::is_invocable_v<OnErr, E const&>
              && std::same_as<std::invoke_result_t<OnOk, T const&>, std::invoke_result_t<OnErr, E const&>>
    constexpr std::invoke_result_t<OnErr, E const&>
    match(OnOk&& on_ok, OnErr&& on_err) const& noexcept(std::is_nothrow_invocable_v<OnOk, T const&>
                                                        && std::is_nothrow_invocable_v<OnErr, E const&>) {
        if (!slot_.holds_()) return static_cast<OnErr&&>(on_err)(slot_.error_());
        return static_cast<OnOk&&>(on_ok)(slot_.value_());
    }
};

namespace detail {

// An Option of a payload.  The concept reads only the reflection of T and
// the template Option, so a unit cannot change its answer.
template <class T>
concept IsOption = ::foundation::reflect::IsInstanceOf<T, ^^Option>;

// A Result of a value and an error.
template <class T>
concept IsResult = ::foundation::reflect::IsInstanceOf<T, ^^Result>;

// The operand of CRUCIBLE_TRY: an rvalue of an Option or a Result that is
// not const, so the macro can consume it.
template <class Hold>
concept TryOperand = std::is_rvalue_reference_v<Hold> && !std::is_const_v<std::remove_reference_t<Hold>>
                  && (IsOption<std::remove_cvref_t<Hold>> || IsResult<std::remove_cvref_t<Hold>>);

// The value of a helper of CRUCIBLE_TRY when the macro refuses its operand.
// The static_assert of the macro stops the compile.  This value converts to
// each type, so the rest of the macro gives no second error.  No run gets
// to the conversion.
struct TryRefused final {
    template <class U>
    [[noreturn]] operator U() const noexcept {
        unreachable();
    }
};

// True when the operand holds no value or holds an error.
template <class Hold>
[[nodiscard]] constexpr bool try_failed_(Hold const& hold) noexcept {
    if constexpr (IsOption<std::remove_cvref_t<Hold>>) {
        return hold.is_none();
    } else if constexpr (IsResult<std::remove_cvref_t<Hold>>) {
        return hold.is_err();
    } else {
        return false;
    }
}

// The return of a failed operand: none for an Option, and the Err of the
// error for a Result.  The return statement of the macro converts it.
template <class Hold>
[[nodiscard]] constexpr auto try_propagate_(Hold&& hold) noexcept {
    using Choice = std::remove_cvref_t<Hold>;
    if constexpr (!TryOperand<Hold&&>) {
        return TryRefused{};
    } else if constexpr (IsOption<Choice>) {
        static_cast<void>(hold);
        return none;
    } else {
        return err(static_cast<Choice&&>(hold).err().expect("CRUCIBLE_TRY tested the error"));
    }
}

// The value of an operand that holds one, and Unit for a Result of Unit,
// whose expect() gives void.
template <class Hold>
[[nodiscard]] constexpr auto try_take_(Hold&& hold) noexcept {
    using Choice = std::remove_cvref_t<Hold>;
    if constexpr (!TryOperand<Hold&&>) {
        return TryRefused{};
    } else if constexpr (std::is_void_v<decltype(static_cast<Choice&&>(hold).expect("a Unit"))>) {
        static_cast<Choice&&>(hold).expect("CRUCIBLE_TRY tested the value");
        return Unit{};
    } else {
        return static_cast<Choice&&>(hold).expect("CRUCIBLE_TRY tested the value");
    }
}

}  // namespace detail

}  // namespace foundation::core

// CRUCIBLE_TRY(name, operand) gives the value of an Option or a Result to a
// new local `name`.  When the operand holds no value, the function returns
// none.  When it holds an error, the function returns the Err of the error,
// and the return converts it to the error of the function.  The operand is
// a temporary or std::move(local), and the macro evaluates it one time.
//
//   CRUCIBLE_TRY(slot, find_slot(key));
//   CRUCIBLE_TRY(_, close_door());        a value of Unit, discarded
//
// The macro is a sequence of statements, so it cannot be the body of an if
// with no braces, and one line holds one CRUCIBLE_TRY.  In a lambda, the
// return leaves the lambda.  The second test of the value is a test that
// the optimizer removes, because no code writes the operand between the
// two tests.
#define CRUCIBLE_TRY_JOIN_(left, right) left##right
#define CRUCIBLE_TRY_HOLD_(line) CRUCIBLE_TRY_JOIN_(crucible_try_hold_, line)
#define CRUCIBLE_TRY(name, ...)                                                                           \
    auto&& CRUCIBLE_TRY_HOLD_(__LINE__) = (__VA_ARGS__);                                                  \
    static_assert(::foundation::core::detail::TryOperand<decltype(CRUCIBLE_TRY_HOLD_(__LINE__))>,         \
                  "CRUCIBLE_TRY consumes a Result or an Option: give it a temporary or std::move(name)"); \
    if (::foundation::core::detail::try_failed_(CRUCIBLE_TRY_HOLD_(__LINE__)))                            \
        return ::foundation::core::detail::try_propagate_(                                                \
            static_cast<decltype(CRUCIBLE_TRY_HOLD_(__LINE__))>(CRUCIBLE_TRY_HOLD_(__LINE__)));           \
    auto name = ::foundation::core::detail::try_take_(                                                    \
        static_cast<decltype(CRUCIBLE_TRY_HOLD_(__LINE__))>(CRUCIBLE_TRY_HOLD_(__LINE__)))
