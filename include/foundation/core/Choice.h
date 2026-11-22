#pragma once

// The Choice family: a value that can be absent, and the result of an
// operation that can fail.
//
// Result<T, E> holds one T or one error E.  A partial operation of the
// base gives a Result, and its caller handles the error with the same
// roads as for an Option: match(), ok(), err(), value_or() and expect().
// CRUCIBLE_TRY takes the value, or returns the error to the caller of the
// function.  map_err(fn) converts the error at a call site, so
// `CRUCIBLE_TRY(fd, open(path).map_err(to_open_error));` returns the error
// of the caller.
// An error is a value: a scoped enum, or a trivially copyable class of at
// most 16 bytes, so an error never allocates.  An error can also be a
// linear class, so a failed operation can give a resource back.  Err<E>
// marks an error, so `return err(Code::full);` and `return value;` both
// convert to the Result.  An Err<F> converts to a Result with the error E
// when F converts to E with no throw.  An Option and a Result do not convert
// to each other: ok() and ok_or() make the crossing.
//
// Option<T> holds one T or no value.  The only roads to the payload are a
// loop of zero or one turns over a niche or a plain Option, the total
// match(), value_or(), ok_or(), the combinators, and expect(), the fatal
// unwrap with a reason.  An owning Option has no loop, because its
// storage counts each lend of its payload and a cursor counts none.  The type has
// no operator* and no operator->, so a read of an empty Option is not
// possible.  The whole type is [[nodiscard]].
//
// A combinator is a match with one arm fixed by the shape of the sum, so
// the caller writes only the arm that does work.  An Option gives map,
// and_then and filter, which consume it, and is_some_and and is_none_or,
// which read it.  A Result gives map, map_err, and_then and or_else, which
// consume it, and is_ok_and and is_err_and, which read it.  A combinator
// is no second road to a result that a chain of the others gives: a
// default after a map is map(fn).value_or(fallback), and a side effect
// belongs in a match.  as_ref(option) of foundation/core/Ref.h gives the
// payload as an Option of a Ref.
//
// The loop and the borrow match read the payload.  The one road that writes
// the payload in place is the frame door option.with_mut(on_some, on_none)
// of foundation/core/Frame.h: on_some gets the RefMut of the payload, and
// the Option keeps it.  A Result gives its value and its error in place
// through the same door, and a OneOf gives its active alternative.
//
// A named Option has three doors that change it and drop no payload.
// take() gives the payload as an Option and leaves the named Option empty.
// fill(value) builds a payload in an empty Option.  replace(value) puts a new
// payload in and gives the old payload back as an Option.  An Option of a
// linear payload has no assignment, but its payload can move through one
// named Option with these doors, as in a loop over a linear session handle.
// expect_none(why) is the fatal end of an empty Option, the twin of expect:
// a full Option ends the process, so no payload drops.  The rule must-consume
// reads the empty state of a named Option through the annotations of
// foundation/Linear.h on these doors, on the two tests and on the two
// constructors of an empty Option.
//
// The storage has three forms, and the payload type selects the form:
//
//   niche    The payload declares an empty value through niche<T>.  The
//            Option then has the size of the payload, and the empty value
//            is the state "no value".
//   plain    A payload with no niche whose copy, move and end are those of
//            its bytes.  The Option holds the payload and a tag, in the
//            layout of a plain Result.  Its copy, move and end are those of
//            its bytes too.  The tag is one byte, or one word of eight bytes
//            after a storage of whole words.  The x86-64 System V ABI then
//            gives it back in registers when it has at most 16 bytes.  The
//            Option is trivially copyable when its payload is.  A sealed
//            payload (foundation/ByteSeal.h) gives the Option an assignment
//            that copies the bytes, so the Option is sealed too.
//   owning   Each other payload.  The Option is move-only.  A move leaves
//            the source empty, and the destructor destroys the payload.
//            The end of a full Option of a linear payload ends the
//            process, because it would drop the payload.
//
// A move of a niche or plain Option is a copy of the bytes, so the source
// keeps its value.  The payload owns nothing in those two forms, so two
// copies of it are safe.  expect(), value_or() and the rvalue match()
// empty the source in each form.
//
// A linear payload (foundation/Linear.h) takes the owning form, also when
// its bytes are trivially copyable, so a move never leaves a second copy
// of the value.  A sum with a linear alternative carries the linear marker
// itself, so the rule must-consume holds the sum to one consume.  An Option
// or a Result gives a linear payload only through a consumer that takes the
// Option or the Result as an rvalue: expect(), ok(), err(), the rvalue
// match(), and the combinators that transform.  A read through a const
// reference consumes nothing.  A consumer that drops a linear part does not
// exist: value_or() drops its fallback, ok() drops the error, the rvalue
// err() drops the value, filter() drops a refused payload, and a move
// through the cursor of a mutable loop empties the payload in place.
//
// The families of foundation/core use only the language, the compiler
// builtins and the headers of the base allowance.  fixy/Core.h puts their
// public names into namespace fixy.  A check of a family does the same
// work in each build and with each contract semantic, and it ends the
// process through fatal() of the Report family.

#include <foundation/Brand.h>
#include <foundation/ByteSeal.h>
#include <foundation/Linear.h>
#include <foundation/OpenExtension.h>
#include <foundation/Platform.h>
#include <foundation/core/Fatal.h>
#include <foundation/core/Frame.h>
#include <foundation/core/Record.h>
#include <foundation/core/Side.h>
#include <foundation/reflect/Instance.h>
#include <foundation/reflect/TypeComponents.h>
#include <foundation/reflect/TypeList.h>

#include <concepts>
#include <cstddef>
#include <meta>
#include <new>
#include <type_traits>

namespace foundation::core {

// The state "no value".  The default constructor is explicit, so a braced
// list with no elements does not convert to it.
struct NoValue final {
    explicit constexpr NoValue() noexcept = default;
};

inline constexpr NoValue none{};

namespace side {

// The file of this header, which anchors the include root of the base in the
// test of the path of a header (the check of the statement).
struct ChoiceHeader final {};

}  // namespace side

class niche_statement;

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

template <class T>
class ConsumedNiche;

// True when the file is a header of the two layers of the base.  The anchor
// is the file of this header, and its include root is the anchor with the
// path foundation/core/Choice.h cut off.  A header of the base is the root,
// then foundation/ or fixy/, with no path segment that starts with a dot,
// such as a segment .. that leaves the layers.  So each include root of a
// build, such as a layer root of the check units, gives one answer, and an
// anchor with a different path admits no file.  Complexity: linear in the
// two texts.
consteval bool is_layer_header(Side<side::ChoiceHeader, char const*> const& anchor, char const* file) {
    char const* const anchor_text = anchor.value();
    char const* const own_path = "foundation/core/Choice.h";
    std::size_t anchor_size = 0;
    while (anchor_text[anchor_size] != '\0')
        ++anchor_size;
    std::size_t own_size = 0;
    while (own_path[own_size] != '\0')
        ++own_size;
    if (anchor_size < own_size) return false;
    std::size_t const root_size = anchor_size - own_size;
    for (std::size_t index = 0; index < own_size; ++index) {
        if (anchor_text[root_size + index] != own_path[index]) return false;
    }
    for (std::size_t index = 0; index < root_size; ++index) {
        if (file[index] != anchor_text[index]) return false;
    }
    char const* const inner = file + root_size;
    constexpr char const* layer_heads[] = {"foundation/", "fixy/"};
    bool is_in_layer = false;
    for (char const* const head : layer_heads) {
        std::size_t same = 0;
        while (head[same] != '\0' && inner[same] == head[same])
            ++same;
        is_in_layer = is_in_layer || head[same] == '\0';
    }
    if (!is_in_layer) return false;
    for (char const* cursor = inner; cursor[0] != '\0'; ++cursor) {
        if (cursor[0] == '/' && cursor[1] == '.') return false;
    }
    return true;
}

// The place of a niche statement: true when the declaration that makes the
// statement stands in a header of the base, at the scope of a class or of a
// namespace.  A mark that the body of a function makes or copies, such as
// the element of a container of the base, reads the name of its function,
// so it is no statement.  Only the default argument of the constructor of
// niche_statement builds a place, because the two constructors are private,
// so no caller names a file.  The copy is user-provided, so std::bit_cast and
// a lifetime start over bytes build no place, for the reasons that niche_key
}  // namespace detail

// The place of a niche statement: true when the declaration that makes the
// statement stands in a header of the base, at the scope of a class or of a
// namespace.  A mark that the body of a function makes or copies, such as
// the element of a container of the base, reads the name of its function,
// so it is no statement.  Only the default argument of the constructor of
// niche_statement builds a place, because the two constructors are private,
// so no caller names a file.  The copy is user-provided, so std::bit_cast and
// a lifetime start over bytes build no place, for the reasons that niche_key
// gives.  __builtin_FILE in the body of the constructor gives the file of
// this header, the anchor, and the build maps each file name in the same way.
class NicheStatementPlace final {
    friend class ::foundation::core::niche_statement;

    consteval explicit NicheStatementPlace(Site const& site) noexcept
        : is_in_base_{detail::is_layer_header(side_as<side::ChoiceHeader>(__builtin_FILE()), detail::site_file_(site))
                      && detail::site_function_(site)[0] == '\0'} {}
    consteval NicheStatementPlace(NicheStatementPlace const& other) noexcept : is_in_base_{other.is_in_base_} {}

    bool is_in_base_ = false;

public:
    NicheStatementPlace& operator=(NicheStatementPlace const&) = delete("a place is the file of one statement");
};

// The statement that no door of a payload builds its empty value (the check
// of the statement).  The default argument of the constructor reads the Site
// where the statement stands, and the payload gate admits only a statement
// of a header of the base at the scope of a class or of a namespace.  A copy
// reads the Site of the copy too, so a declaration of a different file that
// copies a statement of the base states nothing, and so does a mark that the
// body of a function of the base makes or copies.  The copy is user-provided,
// so std::bit_cast builds no mark.
class niche_statement final {
public:
    consteval niche_statement(NicheStatementPlace place = NicheStatementPlace{Site{__builtin_FILE(), __builtin_LINE(),
                                                                                   __builtin_FUNCTION()}}) noexcept
        : is_in_base_{place.is_in_base_} {}
    consteval niche_statement(niche_statement const& other,
                              NicheStatementPlace place = NicheStatementPlace{Site{__builtin_FILE(), __builtin_LINE(),
                                                                                   __builtin_FUNCTION()}}) noexcept
        : is_in_base_{other.is_in_base_ && place.is_in_base_} {}

    niche_statement&
    operator=(niche_statement const&) = delete("a niche statement stands in the file of its declaration");

    // True when the statement stands in a header of the base.
    [[nodiscard]] constexpr bool is_in_base() const noexcept { return is_in_base_; }

private:
    bool is_in_base_ = false;
};

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
//
// niche carries the open mark: only the file that defines a type can
// specialize niche for it (the rule specialization-home).  So the author of
// T writes niche<T>, and the author also writes each door that builds a T.
// The author of a type of the base can then state that no door of T builds
// the empty value, in the specialization or in the class of the member
// protocol:
//
//   static constexpr niche_statement is_empty_never_built{};          in niche<T>
//   static constexpr niche_statement is_niche_empty_never_built{};    in T
//
// The payload gate checks the statement at compile time (the check of the
// statement below): the constructors and the members of T, and the place of
// the mark of the statement in a header of the base.  So a type outside the base states
// no such fact.  The statement gives a loop over a mutable Option a cursor
// that writes the payload, and the storage of a full Option then does not
// test the payload.  The storage of a full Option tests each payload of each
// other niche, in each build, and a payload equal to the empty value ends the
// process.
template <class T>
struct[[= detail::UndeclaredNiche{}]]
      [[= ::foundation::open_extension{"a payload type states its empty value beside its own definition"}]] niche {};

namespace detail {

// True when niche<T> is the primary template, so T declares no niche.
template <class T>
concept UndeclaredNicheOf = ::foundation::linear::carries_annotation(^^niche<T>, ^^UndeclaredNiche);

// A class that names a member of the member protocol, also only the statement
// is_niche_empty_never_built, so a statement with no member pair declares a
// niche that is not well formed, and the payload gate refuses it.
template <class T>
concept NamesMemberNiche =
    requires { &T::niche_empty; } || requires { &T::is_niche_empty; } || requires { &T::is_niche_empty_never_built; };

// A type whose copy, move and end are those of its bytes, and whose copy
// assignment does not throw.  The assignment can be user-provided, as the
// assignment of a type that holds a byte seal is, so std::bit_cast builds
// no such value.  A copy then never allocates.  The niche gate and the
// error gate read this one concept, so their two rules stay the same.
template <class T>
concept TrivialCopyShape = std::is_trivially_copy_constructible_v<T> && std::is_trivially_move_constructible_v<T>
                        && std::is_trivially_destructible_v<T> && std::is_nothrow_copy_assignable_v<T>;

}  // namespace detail

// The niche of a class that gives the members of the protocol.  A class
// that gives only one of the two members, or a member of the wrong type,
// declares a niche that is not well formed, and the payload gate refuses it.
// NicheRead reads the statement of the class in the class itself.
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
// through its private members, and ConsumedNiche writes it into the slot of
// a consumed wrapper.  The gate and the lifetime mark read only its two
// facts, which are constants of type bool, and a decode reads only the bool
// of holds_empty.  So no other code gets an empty value or a key.
template <class T>
class NicheRead final {
    friend class OptionSlot<T, OptionForm::niche>;
    friend class ConsumedNiche<T>;

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
    // True when the value holds the empty value of the niche.  This is the
    // validity test for a value that a bit copy made, such as a value that a
    // decode reads from wire bytes.  No constructor of T made such a value,
    // so it can hold the empty value, which no valid T holds.  The test gives
    // only the bool, and no empty value and no key.
    [[nodiscard]] static constexpr bool holds_empty(T const& value) noexcept
        requires(!UndeclaredNicheOf<T>)
    {
        return is_empty_(value);
    }

    // True when is_empty gives true for the empty value.
    [[nodiscard]] static consteval bool holds_its_empty() noexcept { return is_empty_(empty_()); }

    // True when niche<T>, or T in the member protocol, states that no door of
    // T builds the empty value.  The payload gate holds the type and the
    // place of the statement (the check of the statement).
    [[nodiscard]] static consteval bool is_empty_never_built() noexcept {
        return requires { niche<T>::is_empty_never_built; } || requires { T::is_niche_empty_never_built; };
    }

    // True when each statement of T is a niche_statement, and no reference
    // to one.
    [[nodiscard]] static consteval bool is_statement_well_formed() noexcept {
        bool is_well_formed = true;
        if constexpr (requires { niche<T>::is_empty_never_built; }) {
            is_well_formed = std::same_as<decltype(niche<T>::is_empty_never_built), niche_statement const>;
        }
        if constexpr (requires { T::is_niche_empty_never_built; }) {
            is_well_formed =
                is_well_formed && std::same_as<decltype(T::is_niche_empty_never_built), niche_statement const>;
        }
        return is_well_formed;
    }

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

// Check of the statement
//   A payload that states that no door builds its empty value gets no test
//   of its payload in the storage of a full Option.  So the payload gate
//   holds three checks of the statement (CheckedStatement):
//
//   1. The constructors build no empty value (BuildsNoEmptyValue).
//      - T is no aggregate, and it has no public default constructor, so no
//        code builds a T from its members or from nothing.
//      - Each public constructor other than copy and move that takes a raw
//        value also takes a voucher.  A raw value is a pointer,
//        std::nullptr_t, an arithmetic type, an enumeration, a member
//        pointer or a union, or a class or an array that holds one at some
//        depth, so a plain class around a raw word carries the word.  A
//        class with a part that is not public counts as raw, because the
//        walk does not read its private state.  A voucher is a key, a class
//        with no public constructor, such as RefKey, or a payload whose
//        statement passes the three checks, such as the RunAddress of a
//        DataAddress.  A raw value then comes in only through a door that
//        checks it, or beside a value that is not empty.
//      - A reflection reads no parameter of a constructor template, so T
//        builds no value from std::nullptr_t, a pointer, an int or an
//        unsigned long long at all.
//   2. No public member writes the payload (WritesNoEmptyValue).  The frame
//      door with_mut gives the payload in place, and a local can change
//      before some(), so a member that writes a T could write the empty
//      value.  T has no base class, no public non-static data member, no
//      mutable member, no public member function that is not const other
//      than a constructor and a special member, and no public static member
//      function with a parameter that can write.  GCC 16 reflection gives no
//      const qualifier of a member function template, so T has no public
//      member function template other than a constructor template.
//   3. The statement is of the base (StatedInBase).  The statement is a
//      niche_statement, an own member of niche<T> or of T, and its mark
//      stands in a header of include/foundation or include/fixy.  A
//      reflection reads the members of a class, but it does not see a
//      friend, or what a static door over a private constructor gives.  So
//      only a type whose doors a review of the base reads states the fact,
//      and the author of such a type keeps each friend and each static door
//      to values that are not empty.  The file of a mark is the file that
//      holds it, because the tree holds no #line directive and no line
//      marker (the quarantine plugin and the rule banned refuse each one).
//   A private constructor, such as the constructor of the empty value, is a
//   member of the class of the author, as the statement is.

// True when a payload states that no door builds its empty value, in niche<U>
// or in U itself.
template <class U>
inline constexpr bool states_empty_never_built =
    requires { niche<U>::is_empty_never_built; } || requires { U::is_niche_empty_never_built; };

// True when the statement of the type passes the three checks of the check
// of the statement.  The definition is after the three concepts.
consteval bool is_statement_checked(std::meta::info type);

// True when the member is a niche_statement of a header of the base: a
// variable of the type niche_statement const, whose mark stands in the base.
// The null reflection is no such member.
consteval bool is_base_statement(std::meta::info member) {
    if (member == std::meta::info{} || !std::meta::is_variable(member)) return false;
    if (std::meta::type_of(member) != (^^niche_statement const)) return false;
    return std::meta::extract<niche_statement const&>(member).is_in_base();
}

// True when the statement of the type stands in the base (3 of the check of
// the statement).  A specialization of niche states it in its own member,
// and a class of the member protocol in a member of its own, and each one
// that the type has is a niche_statement of the base.  A statement that a
// class or a niche takes from a base class names no own member, so the
// check refuses it.  A statement is public, because the gate reads it.
// Complexity: linear in the members of the type and of its niche.
consteval bool is_stated_in_base(std::meta::info type) {
    std::meta::info const bare = std::meta::dealias(type);
    std::meta::info const niche_type = std::meta::substitute(^^niche, {bare});
    std::meta::access_context const each = std::meta::access_context::unprivileged();
    std::meta::info const in_class =
        std::meta::is_class_type(bare)
            ? ::foundation::reflect::member_named_in(bare, each, "is_niche_empty_never_built")
            : std::meta::info{};
    std::meta::info const in_niche = ::foundation::reflect::member_named_in(niche_type, each, "is_empty_never_built");
    bool const has_statement = in_class != std::meta::info{} || in_niche != std::meta::info{};
    bool const is_class_statement_held =
        in_class == std::meta::info{} || (std::meta::parent_of(in_class) == bare && is_base_statement(in_class));
    bool const is_niche_statement_held =
        in_niche == std::meta::info{} || (std::meta::parent_of(in_niche) == niche_type && is_base_statement(in_niche));
    return has_statement && is_class_statement_held && is_niche_statement_held;
}

// True when the class has a public constructor that is not deleted.  A
// constructor template counts.  The walks of the check of the statement index
// the lists of a query, as the walks of foundation/core/Owner.h do.
// Complexity: linear in the members.
consteval bool has_public_constructor(std::meta::info type) {
    auto const members = std::meta::members_of(type, std::meta::access_context::unprivileged());
    for (std::size_t index = 0; index < members.size(); ++index) {
        std::meta::info const member = members[index];
        if (std::meta::is_constructor_template(member)) return true;
        if (std::meta::is_function(member) && std::meta::is_constructor(member) && !std::meta::is_deleted(member)) {
            return true;
        }
    }
    return false;
}

// True when the type, with no reference and no cv, vouches for the raw
// values beside it: a key, which is a class with no public constructor, so
// only the doors and the friends of its author build one, or a payload whose
// statement passes the check.  An incomplete class, such as the target of a
// Ref, vouches for nothing.
consteval bool is_voucher_type(std::meta::info type) {
    std::meta::info const bare = std::meta::remove_cvref(std::meta::dealias(type));
    if (!std::meta::is_class_type(bare) || !std::meta::is_complete_type(bare)) return false;
    if (!has_public_constructor(bare)) return true;
    return std::meta::extract<bool>(std::meta::substitute(^^states_empty_never_built, {bare}))
        && is_statement_checked(bare);
}

// True when a value of the type can carry a raw value into a constructor:
// the type is not a class, or it is a class or an array of classes with a
// public base or a public member that carries one.  A reference member holds
// an address, so it carries one too.  A voucher carries none.  An incomplete
// class, and a class with a base or a member that is not public, carry one,
// because the walk cannot read them.  The walk ends, because no class holds
// itself.  Complexity: linear in the subobject declarations that the walk
// reaches.
consteval bool carries_raw_value(std::meta::info type) {
    std::meta::info const bare = std::meta::remove_all_extents(std::meta::remove_cvref(std::meta::dealias(type)));
    if (!std::meta::is_class_type(bare) || !std::meta::is_complete_type(bare)) return true;
    if (is_voucher_type(bare)) return false;
    std::meta::access_context const outside = std::meta::access_context::unprivileged();
    if (std::meta::has_inaccessible_subobjects(bare, outside)) return true;
    auto const bases = std::meta::bases_of(bare, outside);
    for (std::size_t index = 0; index < bases.size(); ++index) {
        if (carries_raw_value(std::meta::type_of(bases[index]))) return true;
    }
    auto const members = std::meta::nonstatic_data_members_of(bare, outside);
    for (std::size_t index = 0; index < members.size(); ++index) {
        std::meta::info const member_type = std::meta::type_of(members[index]);
        if (std::meta::is_reference_type(member_type) || carries_raw_value(member_type)) return true;
    }
    return false;
}

// True when a parameter of the constructor is a voucher.  Complexity: linear
// in the parameters, and the walks of their types.
consteval bool takes_voucher(std::meta::info constructor) {
    auto const parameters = std::meta::parameters_of(constructor);
    for (std::size_t index = 0; index < parameters.size(); ++index) {
        if (is_voucher_type(std::meta::type_of(parameters[index]))) return true;
    }
    return false;
}

// True when a parameter of the constructor carries a raw value.  Complexity:
// linear in the parameters, and the walks of their types.
consteval bool takes_raw_value(std::meta::info constructor) {
    auto const parameters = std::meta::parameters_of(constructor);
    for (std::size_t index = 0; index < parameters.size(); ++index) {
        if (carries_raw_value(std::meta::type_of(parameters[index]))) return true;
    }
    return false;
}

// True when each public constructor of the type other than copy and move
// that takes a raw value also takes a voucher (1 of the check of the
// statement).  A constructor with a voucher needs no walk of its raw values,
// so the walk of the target of a Ref runs for no Ref.  Complexity: linear in
// the members of the type and the parameters of its constructors, and the
// walks of their types.
consteval bool vouches_each_raw_parameter(std::meta::info type) {
    auto const members = std::meta::members_of(type, std::meta::access_context::unprivileged());
    for (std::size_t index = 0; index < members.size(); ++index) {
        std::meta::info const member = members[index];
        bool const is_judged = std::meta::is_function(member) && std::meta::is_constructor(member)
                            && !std::meta::is_copy_constructor(member) && !std::meta::is_move_constructor(member)
                            && !std::meta::is_deleted(member);
        if (is_judged && !takes_voucher(member) && takes_raw_value(member)) return false;
    }
    return true;
}

// True when a parameter of the function is a reference or a pointer to an
// object that is not const, so a static member function can write a T
// through it.  Complexity: linear in the parameters.
consteval bool has_writable_parameter(std::meta::info function) {
    auto const parameters = std::meta::parameters_of(function);
    for (std::size_t index = 0; index < parameters.size(); ++index) {
        std::meta::info const parameter_type = std::meta::type_of(parameters[index]);
        bool const is_indirect =
            std::meta::is_reference_type(parameter_type) || std::meta::is_pointer_type(parameter_type);
        std::meta::info const target = std::meta::is_pointer_type(parameter_type)
                                         ? std::meta::remove_pointer(parameter_type)
                                         : std::meta::remove_reference(parameter_type);
        if (is_indirect && !std::meta::is_const_type(target)) return true;
    }
    return false;
}

// True when no public member of the type writes a value of the type in
// place (2 of the check of the statement).  A deleted function writes
// nothing.  The walk of the fields reads the private fields too, because a
// const member function writes a mutable field of each access.  Complexity:
// linear in the members of the type.
consteval bool writes_no_payload(std::meta::info type) {
    std::meta::access_context const outside = std::meta::access_context::unprivileged();
    if (std::meta::bases_of(type, outside).size() != 0 || std::meta::has_inaccessible_bases(type, outside)) {
        return false;
    }
    auto const fields = std::meta::nonstatic_data_members_of(type, std::meta::access_context::unchecked());
    for (std::size_t index = 0; index < fields.size(); ++index) {
        if (std::meta::is_public(fields[index]) || std::meta::is_mutable_member(fields[index])) return false;
    }
    auto const members = std::meta::members_of(type, outside);
    for (std::size_t index = 0; index < members.size(); ++index) {
        std::meta::info const member = members[index];
        if (std::meta::is_function_template(member) && !std::meta::is_constructor_template(member)) return false;
        bool const is_judged = std::meta::is_function(member) && !std::meta::is_constructor(member)
                            && !std::meta::is_special_member_function(member) && !std::meta::is_deleted(member);
        if (!is_judged) continue;
        bool const can_write =
            std::meta::is_static_member(member) ? has_writable_parameter(member) : !std::meta::is_const(member);
        if (can_write) return false;
    }
    return true;
}

// The constructor check of the statement of T (1 of the check of the
// statement).
template <class T>
concept BuildsNoEmptyValue =
    !std::is_aggregate_v<T> && !std::is_default_constructible_v<T> && !std::is_constructible_v<T, std::nullptr_t>
    && !std::is_constructible_v<T, void*> && !std::is_constructible_v<T, int>
    && !std::is_constructible_v<T, unsigned long long> && vouches_each_raw_parameter(^^T);

// The member check of the statement of T (2 of the check of the statement).
template <class T>
concept WritesNoEmptyValue = writes_no_payload(^^T);

// The place of the statement of T (3 of the check of the statement).
template <class T>
concept StatedInBase = is_stated_in_base(^^T);

// The three checks of the statement of T, in the order of the check of the
// statement.  The two walks come first, so a refusal names the walk that a
// class of the base fails.
template <class T>
concept CheckedStatement = BuildsNoEmptyValue<T> && WritesNoEmptyValue<T> && StatedInBase<T>;

template <class T>
inline constexpr bool is_statement_checked_v = CheckedStatement<T>;

consteval bool is_statement_checked(std::meta::info type) {
    return std::meta::extract<bool>(std::meta::substitute(^^is_statement_checked_v, {type}));
}

// The second reader of the niche of T: the consume tracker of a Linear of the
// test builds (fixy/Qtt.h).  A tracked wrapper of a payload with a niche keeps
// its consume state in the payload, so it has the size of the payload.  A
// consumed wrapper holds the empty value, which no valid T holds.  mark writes
// the empty value only into the slot of a wrapper whose payload the consume
// took, and no code reads that slot as a T again.  holds gives only the bool.
template <class T>
class ConsumedNiche final {
public:
    static constexpr void mark(T& slot) noexcept
        requires(!UndeclaredNicheOf<T>)
    {
        slot = NicheRead<T>::empty_();
    }

    [[nodiscard]] static constexpr bool holds(T const& slot) noexcept
        requires(!UndeclaredNicheOf<T>)
    {
        return NicheRead<T>::is_empty_(slot);
    }
};

}  // namespace detail

// The type declares no niche, or its niche is well formed.  A type declares
// a niche when niche<T> is a specialization.  A well formed niche has the
// trivial copy, move and destructor of the protocol, and its two members
// take the key, have the types of the protocol and do not throw.  Its empty
// value is a constant, and is_empty gives true for it.  A statement that no
// door builds the empty value is a constant of type bool, and it passes the
// three checks of the check of the statement.  A malformed niche
// would give the plain or the owning form with no message, so the payload
// gate refuses it.  This gate is the one gate that reads niche<T>.
template <class T>
concept WellFormedNiche =
    detail::UndeclaredNicheOf<T> || (detail::TrivialCopyShape<T> && requires(T const& value, niche_key<T> const& key) {
        { niche<T>::empty(key) } noexcept -> std::same_as<T>;
        { niche<T>::is_empty(key, value) } noexcept -> std::same_as<bool>;
        typename std::bool_constant<detail::NicheRead<T>::holds_its_empty()>;
    } && detail::NicheRead<T>::holds_its_empty() && detail::NicheRead<T>::is_statement_well_formed() && (!detail::NicheRead<T>::is_empty_never_built() || detail::CheckedStatement<T>));

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

// A sum with an alternative that is not droppable, so the sum must give it out
// exactly one time.  Option, Result and OneOf then carry the linear marker,
// because the union of their storage cannot move, and the trait reads no
// member of a class that cannot move.  The concept reads DroppablePayload, so
// it walks no payload that copies, such as a payload with a niche.
template <class... Ts>
concept HoldsLinearAlternative = (!DroppablePayload<Ts> || ...);

// A payload whose bytes an Option or a Result can copy.  The union of the
// plain slot declares a destructor, so it has no move, and the slot builds
// its copy and its move from the copy constructor of each payload.  So a
// plain payload has a copy constructor that copies its bytes.  A trivially
// copyable payload with that copy is one.  A payload of the shape
// TrivialCopyShape is one too: its copy, move and end are those of its
// bytes, and its assignment can be user-provided, as the assignment of a
// sealed class is.  A move-only payload takes the owning slot, which moves
// it, also when its bytes are trivially copyable.  A payload that copies is
// droppable, so no plain payload is linear.  The ABI passes the sum of plain
// payloads in registers.
template <class T>
concept PlainPayload =
    std::is_trivially_copy_constructible_v<T> && (std::is_trivially_copyable_v<T> || detail::TrivialCopyShape<T>);

// The value of a Result of an operation that gives no value.
struct Unit final {
    [[nodiscard]] friend constexpr bool operator==(Unit, Unit) noexcept = default;
};

// An error: a scoped enum, a class of at most 16 bytes of the shape
// TrivialCopyShape, or a linear class.  A plain integer is refused, because
// its meaning is not in its type.  A copyable class that owns memory is
// refused, because an error never allocates.  A class with a reference
// member is refused, because the reference cannot be assigned and can
// outlive its object.  A sealed class, such as a Ref, has a user-provided
// assignment, and the Result then keeps its plain form with an assignment
// that copies the bytes.  A linear error gives a resource back to the
// caller of a failed operation.  It moves and ends with no throw, and only a
// consumer of the Result gives it out.  An Err marks a return, so it is not
// an error value itself.
template <class E>
concept ErrorValue = !std::is_const_v<E> && !std::is_volatile_v<E> && !detail::ErrMark<E>
                  && (std::is_scoped_enum_v<E> || (std::is_class_v<E> && detail::TrivialCopyShape<E> && sizeof(E) <= 16)
                      || (::foundation::linear::is_linear_type(^^E)
                          && std::is_nothrow_move_constructible_v<E> && std::is_nothrow_destructible_v<E>));

// An error F that a Result with the error E takes from an Err<F>: a
// different error type, with an implicit conversion to E that does not
// throw.  An explicit conversion is refused, because the return of an Err
// shows no conversion.
template <class F, class E>
concept ErrorConvertsTo = ErrorValue<F> && ErrorValue<E> && !std::same_as<F, E> && std::is_nothrow_convertible_v<F, E>;

// The alternatives of a OneOf (foundation/core/OneOf.h): 2 to 255 payloads,
// with no type two times, so a value selects one alternative.  The value of
// the tag of a OneOf is the index of the active alternative, so it fits in
// one byte.  OccursOnce is a concept, so no unit can change what the gate
// admits.
template <class... Ts>
concept OneOfAlternatives = sizeof...(Ts) >= 2 && sizeof...(Ts) <= 255 && (ChoicePayload<Ts> && ...)
                         && (::foundation::reflect::OccursOnce<Ts, Ts...> && ...);

template <ChoicePayload T>
class Option;

template <ChoicePayload T, ErrorValue E>
class Result;

namespace detail {

// An Option of a payload.  The concept reads only the reflection of T and
// the template Option, so a unit cannot change its answer.  It forms no
// Option of a deduced payload, so it checks no gate of the payload and
// reads no open template such as niche.  A partial specialization over
// Option<T> would form Option<T> and read the payload gate, which reads
// niche, and the rule home refuses that.  The combinators of Option and
// Result read the two tests, so the tests come before the two classes.
template <class T>
concept IsOption = ::foundation::reflect::IsInstanceOf<T, ^^Option>;

// The payload of an Option, from the reflection of its one template
// argument.  An Atomic cell of an Option takes the word of this payload.
template <IsOption T>
using option_payload_t = [:std::meta::template_arguments_of(std::meta::dealias(std::meta::remove_cvref(^^T)))[0]:];

// A Result of a value and an error.
template <class T>
concept IsResult = ::foundation::reflect::IsInstanceOf<T, ^^Result>;

// The value type of a Result, from the reflection of its first template
// argument.  or_else gives a Result of the same value type.
template <IsResult T>
using result_value_t = [:std::meta::template_arguments_of(std::meta::dealias(std::meta::remove_cvref(^^T)))[0]:];

template <bool IsPlain, class... Ts>
class SumSlotOf;

// The function type of the address of a call: the function of a pointer to a
// member function, and the function of a pointer to a static call operator.
template <class Address>
struct CallFunction final {
    using type = std::remove_pointer_t<Address>;
};

template <class Function, class Owner>
struct CallFunction<Function Owner::*> final {
    using type = Function;
};

// The type of the one parameter of a function type, or the null reflection.
// An explicit object parameter counts, so a call operator with one gives two.
[[nodiscard]] consteval std::meta::info one_parameter_of_(std::meta::info function_type) {
    auto const parameter_types = std::meta::parameters_of(std::meta::dealias(function_type));
    return parameter_types.size() == 1 ? parameter_types[0] : std::meta::info{};
}

// The one call operator of a class, or the null reflection when the class has
// no call operator or more than one.  A call operator template counts.
// Complexity: linear in the members of the class.
template <class Bare>
[[nodiscard]] consteval std::meta::info one_call_operator_of_() {
    std::meta::info call{};
    std::size_t call_count = 0;
    for (std::meta::info const member : std::meta::members_of(^^Bare, std::meta::access_context::current())) {
        bool const is_operator =
            std::meta::is_operator_function(member) || std::meta::is_operator_function_template(member);
        if (!is_operator || std::meta::operator_of(member) != std::meta::operators::op_parentheses) continue;
        call = member;
        ++call_count;
    }
    return call_count == 1 ? call : std::meta::info{};
}

// True when the arm is a class whose one call operator is a template.  The
// address of a call operator that is no template and has no overload exists,
// so such an arm takes no walk of the members.
template <class Arm>
[[nodiscard]] consteval bool calls_through_template_() {
    using Bare = std::remove_cvref_t<Arm>;
    if constexpr (!std::is_class_v<Bare> || requires { &Bare::operator(); }) {
        return false;
    } else {
        std::meta::info const call = detail::one_call_operator_of_<Bare>();
        return call != std::meta::info{} && std::meta::is_function_template(call);
    }
}

// The type of the parameter of the call of an arm with one rvalue of the
// payload, or the null reflection.  A function, and a pointer or a reference
// to one, gives its one parameter.  A class gives the one parameter of its one
// call operator.  The address of a call operator that is no template and has
// no overload gives its type with no walk of the members.  A call operator
// that is a template takes the payload as its one template argument, as a
// deduction from an rvalue of the payload gives it.  Each other shape gives
// the null reflection: no call operator or more than one, a call of more than
// one parameter, an explicit object parameter, and a template that the
// payload does not fit.  The function is a template, so only a unit that
// matches a sum reads an arm.  Complexity: constant for a function and for a
// call operator that is no template, else linear in the members of the arm.
template <class Arm, class Payload>
[[nodiscard]] consteval std::meta::info arm_parameter_of_() {
    using Bare = std::remove_cvref_t<Arm>;
    if constexpr (std::is_function_v<std::remove_pointer_t<Bare>>) {
        return detail::one_parameter_of_(^^std::remove_pointer_t<Bare>);
    } else if constexpr (requires { &Bare::operator(); }) {
        return detail::one_parameter_of_(^^typename CallFunction<decltype(&Bare::operator())>::type);
    } else if constexpr (!std::is_class_v<Bare>) {
        return std::meta::info{};
    } else {
        std::meta::info call = detail::one_call_operator_of_<Bare>();
        if (call == std::meta::info{}) return std::meta::info{};
        if (std::meta::is_function_template(call)) {
            if (!std::meta::can_substitute(call, {^^Payload})) return std::meta::info{};
            call = std::meta::substitute(call, {^^Payload});
        }
        auto const parameters = std::meta::parameters_of(call);
        if (parameters.size() != 1 || std::meta::is_explicit_object_parameter(parameters[0])) return std::meta::info{};
        return std::meta::type_of(parameters[0]);
    }
}

// True when the arm takes the payload by value: the parameter of its call is
// the payload type, with no reference.  An arm whose call reflection cannot
// read fails closed.
template <class Arm, class Payload>
[[nodiscard]] consteval bool takes_payload_by_value_() {
    std::meta::info const parameter = detail::arm_parameter_of_<Arm, Payload>();
    return parameter != std::meta::info{} && !std::meta::is_reference_type(parameter)
        && std::meta::dealias(std::meta::remove_cv(parameter)) == std::meta::dealias(^^Payload);
}

// True when the parameter reads the payload with no conversion: it is the
// payload type, or an lvalue reference to the const payload type.  The null
// reflection gives false.
template <class Payload>
[[nodiscard]] consteval bool reads_payload_exactly_(std::meta::info parameter) {
    if (parameter == std::meta::info{}) return false;
    std::meta::info const payload = std::meta::dealias(^^Payload);
    if (!std::meta::is_reference_type(parameter)) return std::meta::dealias(std::meta::remove_cv(parameter)) == payload;
    if (!std::meta::is_lvalue_reference_type(parameter)) return false;
    std::meta::info const referred = std::meta::dealias(std::meta::remove_reference(parameter));
    return std::meta::is_const_type(referred) && !std::meta::is_volatile_type(referred)
        && std::meta::dealias(std::meta::remove_cv(referred)) == payload;
}

// True when the arm of a borrow match reads the payload with no conversion:
// the parameter of its call is the payload type or a const reference to it.
// An arm whose call reflection cannot read fails closed.
//
// A call operator that is a template passes with no read of its parameter.
// The match gives it a const lvalue of the payload, and a deduction from that
// lvalue gives the payload type, a const reference to it, or a reference to a
// base of it, and never a class that converts.  Reflection reads no parameter
// of a template, and a substitution instantiates the body of the arm.  A
// substitution with the payload would give an arm of the form auto&& an
// rvalue, which the match never gives it, and its body can refuse an rvalue.
// So the gate does not see a template whose payload parameter names no
// template parameter, such as a call with an explicit object parameter.
template <class Arm, class Payload>
[[nodiscard]] consteval bool takes_payload_by_borrow_() {
    if constexpr (detail::calls_through_template_<Arm>()) {
        return true;
    } else {
        return detail::reads_payload_exactly_<Payload>(detail::arm_parameter_of_<Arm, Payload>());
    }
}

// True when a value of the type holds a borrow: a reference, a pointer, a
// pointer to a member, or a class whose state reflection cannot read, such as
// a closure with captures on a compiler without the sixth GCC patch.  A
// closure is a class whose captures are its members.  The walk reads each
// element of an array, and each
// base and each non-static data member of a class or a union.  An Option and
// a Result hold only their payloads and a tag, so the walk reads their
// arguments and not their slots.  A OneOf holds one sum slot, and the walk
// reads the alternatives of the slot, after its first argument, the form of
// the slot.  OneOf.h defines OneOf, so this header names no OneOf.
// Complexity: linear in the parts of the type.
[[nodiscard]] consteval bool holds_borrow_(std::meta::info type) {
    if (std::meta::is_reference_type(type)) return true;
    type = std::meta::dealias(std::meta::remove_cv(std::meta::dealias(type)));
    if (std::meta::is_pointer_type(type) || std::meta::is_member_pointer_type(type)) return true;
    if (std::meta::is_array_type(type)) return detail::holds_borrow_(std::meta::remove_all_extents(type));
    if (!std::meta::is_class_type(type) && !std::meta::is_union_type(type)) return false;
    if (std::meta::has_template_arguments(type)) {
        std::meta::info const family = std::meta::template_of(type);
        bool const is_slot = family == ^^SumSlotOf;
        if (is_slot || family == ^^Option || family == ^^Result) {
            auto const payloads = std::meta::template_arguments_of(type);
            for (std::size_t index = is_slot ? 1 : 0; index < payloads.size(); ++index) {
                if (detail::holds_borrow_(payloads[index])) return true;
            }
            return false;
        }
    }
    ::foundation::reflect::TypeNode const node{type, true};
    if (::foundation::reflect::holds_unreadable_state(node)) return true;
    for (::foundation::reflect::TypeNode const& part : ::foundation::reflect::member_components_of(node)) {
        if (!part.is_held_by_value || detail::holds_borrow_(part.type)) return true;
    }
    return false;
}

// The template arguments of a type, by index.  The walk indexes the list, so
// it makes no static list.
[[nodiscard]] consteval std::size_t template_argument_count(std::meta::info type) {
    return std::meta::template_arguments_of(type).size();
}

[[nodiscard]] consteval std::meta::info template_argument_at(std::meta::info type, std::size_t index) {
    return std::meta::template_arguments_of(type)[index];
}

// True when the type is the brand, or names the brand at a depth of its
// template arguments.  The walk reads each template argument that is a
// type.  The function is not a template, so no unit can specialize it and
// change what a gate admits.  Complexity: O(A^2) for A template arguments
// at one depth, because each index reads the list again, at compile time
// only.
[[nodiscard]] consteval bool names_brand(std::meta::info type, std::meta::info brand) {
    std::meta::info const bare = std::meta::dealias(std::meta::remove_cvref(type));
    if (bare == brand) {
        return true;
    }
    if (!std::meta::has_template_arguments(bare)) {
        return false;
    }
    std::size_t const count = template_argument_count(bare);
    for (std::size_t index = 0; index < count; ++index) {
        std::meta::info const argument = template_argument_at(bare, index);
        if (std::meta::is_type(argument) && names_brand(argument, brand)) {
            return true;
        }
    }
    return false;
}

}  // namespace detail

// An arm of an rvalue match that takes its payload by value.  The match ends
// the payload, so a parameter of a reference type would name an object that
// ends.  Reflection reads the call of the arm, and an arm whose call it cannot
// read is refused.
template <class Arm, class Payload>
concept TakesPayloadByValue = detail::takes_payload_by_value_<Arm, Payload>();

// An arm of a borrow match that reads its payload with no conversion: the
// parameter of its call is the payload type or a const reference to it.  An
// implicit constructor of another class would otherwise read the payload as
// that class, and the arms of a OneOf in the wrong order would each read the
// other alternative.  A generic arm passes, because a deduction from the
// payload gives the payload type.  Reflection reads the call of the arm, and
// an arm whose call it cannot read is refused.
template <class Arm, class Payload>
concept TakesPayloadByBorrow = detail::takes_payload_by_borrow_<Arm, Payload>();

// A result of an rvalue match that holds no borrow.  The match ends the
// payload, so a reference or an address in the result could name an object
// that ends.  The concept refuses each reference and each address, also an
// address of an object that the payload does not hold.  An arithmetic value,
// an enumerator and void pass with no reflection.
template <class Given>
concept HoldsNoBorrow =
    std::is_arithmetic_v<Given> || std::is_enum_v<Given> || std::is_void_v<Given> || !detail::holds_borrow_(^^Given);

// A result of the body of a frame door (foundation/core/Frame.h): void, or a
// value that holds no borrow, names no brand of the frame and fits a Record.
// The door gives the value and the owner back in one Record, so the value
// leaves the frame.  A unique borrow or a Ref of the frame in it would
// outlive the frame, and so would a brand that proves something about the
// frame.  A const value goes into the Record with no const.
template <class Given, class Brand>
concept FrameResult =
    std::is_void_v<Given>
    || (HoldsNoBorrow<Given> && !detail::names_brand(^^Given, ^^Brand) && RecordElement<std::remove_cv_t<Given>>);

namespace detail {

// The type that a frame door gives back: the owner alone for a body that
// gives void, and a Record of the result and the owner for each other body.
template <class Given, class Owner>
struct FrameReturn {
    using type = Record<std::remove_cv_t<Given>, Owner>;
};

template <class Owner>
struct FrameReturn<void, Owner> {
    using type = Owner;
};

template <class Given, class Owner>
using frame_return_t = typename FrameReturn<Given, Owner>::type;

}  // namespace detail

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

// The mark closed_tag of a class whose bytes hold the tag of a sum is in
// foundation/ByteSeal.h, so the checked lifetime start reads it too.

namespace detail {

struct NoPayload final {};

// Lend count
//     A borrow match gives an arm a reference into the payload.  The rule
//     borrow-match reads one body, so a write in a callee of the arm or
//     through a second reference escapes it.  The owning storage counts the
//     borrow matches that lend its payload.  A write that ends the payload
//     while the count is not zero ends the process, in each build.  The count
//     is mutable, because a match takes a const reference, and its changes
//     are relaxed atomic steps, so two threads can match one sum.  It holds
//     255 lends, and one more ends the process.

[[noreturn]] CRUCIBLE_COLD inline void report_lent_write_() noexcept {
    fatal("a write ends the payload of a sum while a borrow match lends that payload to an arm");
}

[[noreturn]] CRUCIBLE_COLD inline void report_lend_overflow_() noexcept {
    fatal("more than 255 borrow matches lend the payload of one sum at one time");
}

[[noreturn]] CRUCIBLE_COLD inline void report_full_fill_() noexcept {
    fatal("Option::fill got a full Option, and a fill would drop the payload that it holds");
}

[[noreturn]] CRUCIBLE_COLD inline void report_full_linear_end_() noexcept {
    fatal("an Option of a linear payload ended with its payload: consume it with take, or end it with a named door");
}

[[noreturn]] CRUCIBLE_COLD inline void report_owed_alternative_end_() noexcept {
    fatal("a Result or a OneOf ended with a linear alternative that no consumer took: give it to expect, the rvalue "
          "match or a combinator");
}

// End of a linear alternative
//   A Result and a OneOf have no empty state, so a consumer leaves the
//   moved-from alternative in place, and the end of the sum ends it.  A test
//   build (CRUCIBLE_QTT_TRACK_CONSUME, as the consume tracker of fixy/Qtt.h
//   reads it) keeps one mark beside an owning sum with a linear alternative.
//   A consumer sets the mark, and so does the move of the sum, for its
//   source.  The end of a sum whose active alternative is linear and whose
//   mark is clear ends the process.  The rule must-consume refuses such an
//   end at compile time where it sees it, so the mark is a test oracle of
//   the rule.  A Release build and a sum with no linear alternative keep an
//   empty mark, so their layout and their code do not change.
#ifdef CRUCIBLE_QTT_TRACK_CONSUME
inline constexpr bool tracks_alternative_ends_ = CRUCIBLE_QTT_TRACK_CONSUME != 0;
#else
inline constexpr bool tracks_alternative_ends_ = false;
#endif

template <bool IsTracked>
struct EndMark {
    constexpr void spend() noexcept {}
    constexpr void require_spent(bool /*is_linear_alternative*/) const noexcept {}
};

template <>
struct EndMark<true> {
    bool is_spent_ = false;

    constexpr void spend() noexcept { is_spent_ = true; }
    constexpr void require_spent(bool is_linear_alternative) const noexcept {
        if (is_linear_alternative && !is_spent_) [[unlikely]] {
            report_owed_alternative_end_();
        }
    }
};

constexpr void add_lend_(unsigned char& lend_count) noexcept {
    if consteval {
        if (lend_count == 255) report_lend_overflow_();
        ++lend_count;
    } else {
        if (__atomic_fetch_add(&lend_count, 1, __ATOMIC_RELAXED) == 255) [[unlikely]] {
            report_lend_overflow_();
        }
    }
}

constexpr void drop_lend_(unsigned char& lend_count) noexcept {
    if consteval {
        --lend_count;
    } else {
        __atomic_fetch_sub(&lend_count, 1, __ATOMIC_RELAXED);
    }
}

// Ends the process while a borrow match lends the payload.  The read is a
// plain load: a write during a match of a different thread is a race on the
// sum itself, so only the lends of this thread count.
constexpr void refuse_lent_write_(unsigned char const& lend_count) noexcept {
    if (lend_count != 0) [[unlikely]] {
        report_lent_write_();
    }
}

// The lend of one borrow match of an owning sum, until its destructor.
class [[nodiscard]] Lend final {
    unsigned char& lend_count_;

public:
    constexpr explicit Lend(unsigned char& lend_count) noexcept : lend_count_{lend_count} {
        detail::add_lend_(lend_count_);
    }
    constexpr ~Lend() { detail::drop_lend_(lend_count_); }

    Lend(Lend const&) = delete("a lend lives in one borrow match");
    Lend& operator=(Lend const&) = delete("a lend lives in one borrow match");
};

// A plain or a niche sum has no destructor, so its match counts no lend.
struct NoLend final {};

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
    [[nodiscard]] constexpr NoLend lend_() const noexcept { return NoLend{}; }
    constexpr void expect_value_(Fmt<> why) const noexcept {
        if (!holds_()) [[unlikely]] {
            fatal(why);
        }
    }
    [[nodiscard]] constexpr bool same_state_(OptionSlot const& other) const noexcept {
        return holds_() == other.holds_();
    }
    [[nodiscard]] constexpr T& payload_() noexcept { return value_; }
    [[nodiscard]] constexpr T const& payload_() const noexcept { return value_; }
    constexpr void clear_() noexcept { value_ = NicheRead<T>::empty_(); }

    // True when the slot tests each payload: the niche does not state that
    // no door of T builds the empty value.  The payload gate checks a
    // statement against the constructors and the members of T, and only a
    // type of the base states one (the check of the statement), so a payload
    // of a stated niche is never the empty value.
    static constexpr bool is_payload_tested_ = !NicheRead<T>::is_empty_never_built();

    // Builds the payload of an empty slot.  The test of the constructor runs
    // first, so the slot never holds the empty value as a payload.
    template <class U>
    constexpr void fill_(U&& value) noexcept(std::is_nothrow_constructible_v<T, U&&>) {
        T const built(static_cast<U&&>(value));
        if constexpr (is_payload_tested_) {
            if (NicheRead<T>::is_empty_(built)) [[unlikely]] {
                fatal("Option::fill got the empty value of the niche of its payload");
            }
        }
        value_ = built;
    }

public:
    constexpr OptionSlot() noexcept = default;

    template <class U>
    constexpr OptionSlot(SomeTag, U&& value) noexcept(std::is_nothrow_constructible_v<T, U&&>)
        : value_(static_cast<U&&>(value)) {
        // A payload equal to the empty value would read as no value, so the
        // slot of each niche with no statement tests it in each build.
        if constexpr (is_payload_tested_) {
            if (NicheRead<T>::is_empty_(value_)) [[unlikely]] {
                fatal("Option::some got the empty value of the niche of its payload");
            }
        }
    }
};

// The one layout rule of the plain forms of the family: the storage, and
// then the tag, as in std::expected and std::variant.  The x86-64 System V
// ABI gives a class of at most 16 bytes back in two registers.  For a
// storage of four bytes, the storage is the low word of the first register,
// so a value goes in and out with no shift, and a test of a tag of one byte
// at bit 32 takes one shift.  A storage that fills whole words of eight
// bytes puts the tag in a register of its own, and the tag is then a word
// of eight bytes.  The cells then hold no padding byte, so a copy of the
// cells carries no byte that -ftrivial-auto-var-init made zero, and an
// inlined function gives its sum back in registers with no store to the
// stack.  The plain Option, and the sums whose alternatives are all plain,
// use this rule.  Only the slot of a plain sum reads the two cells.  The tag
// names one alternative, so the cells carry the mark closed_tag.
template <class Storage, class Tag>
class[[= ::foundation::core::closed_tag{}]] TaggedCells {
    static_assert(std::is_unsigned_v<Tag> && alignof(Tag) == sizeof(Tag) && sizeof(Tag) <= alignof(Storage),
                  "the tag of the cells is one unsigned word, at most as wide as the alignment of the storage");

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

// The arms of the frame door of an Option: on_some takes the RefMut of the
// payload, on_none takes nothing, each with no throw, and the two give one
// type.  A linear payload has no frame door, because an arm could move the
// payload out through the RefMut and leave the Option full.  The door reads
// the niche of the payload in its body (Option::with_mut).
template <class T, class OnSome, class OnNone, class Brand>
concept OptionFrameArms =
    DroppablePayload<T> && std::is_nothrow_invocable_v<OnSome&&, RefMut<T, Brand>>
    && std::is_nothrow_invocable_v<OnNone&&>
    && std::same_as<std::invoke_result_t<OnSome&&, RefMut<T, Brand>>, std::invoke_result_t<OnNone&&>>;

// The arms of the frame door of a Result: on_ok takes the RefMut of the
// value and on_err the RefMut of the error, each with no throw, and the two
// give one type.  A linear value or error has no frame door, for the reason
// of OptionFrameArms.
template <class T, class E, class OnOk, class OnErr, class Brand>
concept ResultFrameArms =
    DroppablePayload<T> && DroppablePayload<E> && std::is_nothrow_invocable_v<OnOk&&, RefMut<T, Brand>>
    && std::is_nothrow_invocable_v<OnErr&&, RefMut<E, Brand>>
    && std::same_as<std::invoke_result_t<OnOk&&, RefMut<T, Brand>>, std::invoke_result_t<OnErr&&, RefMut<E, Brand>>>;

template <ChoicePayload T>
class [[nodiscard]][[= ::foundation::linear::linear_when<HoldsLinearAlternative<T>>]][[= ::foundation::linear::sum{}]]
    Option {
    detail::OptionSlot<T> slot_;

    template <class U>
    constexpr Option(detail::SomeTag tag, U&& value) noexcept(std::is_nothrow_constructible_v<T, U&&>)
        : slot_{tag, static_cast<U&&>(value)} {}

public:
    // The two constructors of an empty Option carry the annotation empties,
    // so the rule must-consume reads an Option that they build as empty.
    [[= ::foundation::linear::empties{}]] constexpr Option() noexcept = default;
    [[= ::foundation::linear::empties{}]] constexpr Option(NoValue) noexcept {}

    template <class F>
    Option(Err<F>) = delete("a Result carries an error that an Option drops: write std::move(result).ok()");

    // A full Option.  The annotation fills tells the rule must-consume that
    // the Option that some builds is full.
    template <class U>
        requires std::constructible_from<T, U&&>
    [[= ::foundation::linear::fills{}]] [[nodiscard]] static constexpr Option
        some(U&& value) noexcept(std::is_nothrow_constructible_v<T, U&&>) {
        return Option{detail::SomeTag{}, static_cast<U&&>(value)};
    }

    // The two tests of the state.  The rule must-consume reads a test of a
    // named Option in the condition of an if statement, so an Option that
    // the test finds empty can end with no consume.
    [[= ::foundation::linear::tests_full{}]] [[nodiscard]] constexpr bool is_some() const noexcept {
        return slot_.holds_();
    }
    [[= ::foundation::linear::tests_empty{}]] [[nodiscard]] constexpr bool is_none() const noexcept {
        return !slot_.holds_();
    }

    // The fatal unwrap at a boundary.  The reason tells why the Option
    // holds a value there.  An empty Option gives the reason and the place
    // of the call to fatal(), and the process ends.  The Option is empty
    // after the call.  A passing expect() costs one branch.  The rule
    // must-consume refuses expect() on an Option that it shows empty
    // (needs_full), so the branch stays only where the state is not known.
    [[= ::foundation::linear::needs_full{}]] [[nodiscard]] constexpr T expect(Fmt<> why) && noexcept {
        slot_.expect_value_(why);
        T taken(static_cast<T&&>(slot_.payload_()));
        slot_.clear_();
        return taken;
    }

    // The fatal end of an empty Option.  The reason tells why the Option
    // holds no value there.  A full Option gives the reason and the place of
    // the call to fatal(), and the process ends, so no payload drops.  The
    // door takes the Option as an rvalue, so the rule must-consume counts it
    // as a consume, also of a temporary, such as a refused upgrade.  A
    // passing expect_none() costs one branch.  The rule must-consume refuses
    // expect_none() on an Option that it shows full (needs_empty).
    [[= ::foundation::linear::needs_empty{}]] constexpr void expect_none(Fmt<> why) && noexcept {
        if (slot_.holds_()) [[unlikely]] {
            fatal(why);
        }
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

    // Doors of a named Option
    //   take, fill and replace change an Option that the caller names, and no
    //   one of them drops a payload, so each one is legal for a linear
    //   payload.  A loop over a linear session handle keeps the handle in one
    //   Option: each turn takes the handle, steps it, and fills the Option
    //   with the next handle.  take and replace give the old payload back as
    //   an Option, which is [[nodiscard]], and an Option of a linear payload
    //   is linear, so the rule must-consume holds it to one consume.  An
    //   owning Option ends its old payload through its clear, so a borrow
    //   match that lends the payload ends the process (the lend count).

    // The payload as a full Option, or an empty Option.  This Option is
    // empty after the call.  The cost is the one branch of expect.
    [[= ::foundation::linear::empties{}]] [[nodiscard]] constexpr Option take() & noexcept {
        if (!slot_.holds_()) return Option{none};
        Option taken{detail::SomeTag{}, static_cast<T&&>(slot_.payload_())};
        slot_.clear_();
        return taken;
    }

    void take() && = delete("a take of a temporary Option empties an object that the full expression ends: use "
                            "expect(), the rvalue match or a combinator");
    void take() const&& = delete("a take of a temporary Option empties an object that the full expression ends: use "
                                 "expect(), the rvalue match or a combinator");

    // Builds the payload from value in this empty Option, in place.  A full
    // Option would drop its payload, so it ends the process.  The check is a
    // check of the family and not a contract, so it holds in each build and
    // with each contract semantic, and this header needs no contracts header.
    // The cost is one test of the state and the construction of the payload.
    // The rule must-consume refuses fill() on an Option that it shows full
    // (needs_empty), so the test stays only where the state is not known.
    template <class U>
        requires std::constructible_from<T, U&&>
    [[= ::foundation::linear::fills{}]][[= ::foundation::linear::needs_empty{}]] constexpr void
        fill(U&& value) & noexcept(std::is_nothrow_constructible_v<T, U&&>) {
        if (slot_.holds_()) [[unlikely]] {
            detail::report_full_fill_();
        }
        slot_.fill_(static_cast<U&&>(value));
    }

    template <class U>
    void fill(U&& value) && = delete("a fill of a temporary Option builds a payload that the full expression ends: "
                                     "name the Option, or build a full Option with some(value)");

    // Puts the payload built from value into this Option, and gives the old
    // payload back as an Option, which is empty when this Option was empty.
    // The new payload is built first, because value can live inside the old
    // payload.  The cost is the branch of take and three moves of a payload,
    // which the optimizer merges for a payload whose move copies its bytes.
    template <class U>
        requires std::constructible_from<T, U&&>
    [[= ::foundation::linear::fills{}]] [[nodiscard]] constexpr Option
        replace(U&& value) & noexcept(std::is_nothrow_constructible_v<T, U&&>) {
        T fresh(static_cast<U&&>(value));
        Option old = take();
        slot_.fill_(static_cast<T&&>(fresh));
        return old;
    }

    template <class U>
    void replace(U&& value) && = delete("a replace of a temporary Option puts a payload into an object that the full "
                                        "expression ends: name the Option, or build a full Option with some(value)");

    // Combinators
    //   A combinator is the rvalue match with the empty arm fixed by the
    //   shape of the sum: the absence passes through, so the caller writes
    //   only the arm of the payload.  map, and_then and filter consume the
    //   Option through && and leave it empty, as the rvalue match does.  fn
    //   takes the payload by value, and its result holds no borrow, by the
    //   gates of the rvalue match.  A linear payload moves into fn, so map and
    //   and_then consume it one time.  filter drops a refused payload, so a
    //   linear payload has no filter.  is_some_and and is_none_or only read,
    //   so they take a const reference and consume nothing.  Each body tests
    //   the absence first, as the match does, and costs the one branch of the
    //   match.

    // The result of fn on the payload, as a full Option, or an empty Option.
    template <class Fn>
        requires std::is_invocable_v<Fn, T&&> && TakesPayloadByValue<Fn, T>
              && ChoicePayload<std::invoke_result_t<Fn, T&&>> && HoldsNoBorrow<std::invoke_result_t<Fn, T&&>>
    [[nodiscard]] constexpr Option<std::invoke_result_t<Fn, T&&>>
    map(Fn&& fn) && noexcept(std::is_nothrow_invocable_v<Fn, T&&>) {
        using Mapped = Option<std::invoke_result_t<Fn, T&&>>;
        if (!slot_.holds_()) return Mapped{none};
        T taken(static_cast<T&&>(slot_.payload_()));
        slot_.clear_();
        return Mapped::some(static_cast<Fn&&>(fn)(static_cast<T&&>(taken)));
    }

    // An fn of map that takes its payload by reference binds to this
    // overload, so the refusal gives its reason.
    template <class Fn>
        requires std::is_invocable_v<Fn, T&&> && (!TakesPayloadByValue<Fn, T>)
    void map(Fn&& fn) && = delete(
        "the function of map takes the payload by value, so it names no object that map ends: write the parameter as "
        "a value, or name the Option and match its borrow");

    // The Option that fn gives for the payload, or an empty Option.
    template <class Fn>
        requires std::is_invocable_v<Fn, T&&> && TakesPayloadByValue<Fn, T>
              && detail::IsOption<std::invoke_result_t<Fn, T&&>> && HoldsNoBorrow<std::invoke_result_t<Fn, T&&>>
    [[nodiscard]] constexpr std::invoke_result_t<Fn, T&&>
    and_then(Fn&& fn) && noexcept(std::is_nothrow_invocable_v<Fn, T&&>) {
        if (!slot_.holds_()) return none;
        T taken(static_cast<T&&>(slot_.payload_()));
        slot_.clear_();
        return static_cast<Fn&&>(fn)(static_cast<T&&>(taken));
    }

    // An fn of and_then that takes its payload by reference binds to this
    // overload, so the refusal gives its reason.
    template <class Fn>
        requires std::is_invocable_v<Fn, T&&> && (!TakesPayloadByValue<Fn, T>)
    void and_then(Fn&& fn) && = delete(
        "the function of and_then takes the payload by value, so it names no object that and_then ends: write the "
        "parameter as a value, or name the Option and match its borrow");

    // The Option when keep accepts its payload, else an empty Option.  The
    // payload moves out first, so keep reads a local through a const
    // reference, and no write through keep reaches the Option.
    template <class Keep>
        requires DroppablePayload<T> && std::is_invocable_v<Keep&, T const&>
              && std::same_as<std::invoke_result_t<Keep&, T const&>, bool>
    [[nodiscard]] constexpr Option filter(Keep&& keep) && noexcept(std::is_nothrow_invocable_v<Keep&, T const&>) {
        if (!slot_.holds_()) return Option{none};
        T taken(static_cast<T&&>(slot_.payload_()));
        slot_.clear_();
        if (!keep(static_cast<T const&>(taken))) return Option{none};
        return Option{detail::SomeTag{}, static_cast<T&&>(taken)};
    }

    // True when the Option holds a payload that holds accepts.  holds reads
    // the payload through a const reference, the Option keeps it, and an
    // owning Option counts the lend (the lend count).
    template <class Holds>
        requires std::is_invocable_v<Holds&, T const&> && std::same_as<std::invoke_result_t<Holds&, T const&>, bool>
    [[= ::foundation::linear::borrow_match{}]] [[nodiscard]] constexpr bool is_some_and(Holds&& holds) const
        noexcept(std::is_nothrow_invocable_v<Holds&, T const&>) {
        if (!slot_.holds_()) return false;
        [[maybe_unused]] auto const lend = slot_.lend_();
        return holds(slot_.payload_());
    }

    // True when the Option is empty, or when holds accepts its payload.  The
    // read is that of is_some_and.
    template <class Holds>
        requires std::is_invocable_v<Holds&, T const&> && std::same_as<std::invoke_result_t<Holds&, T const&>, bool>
    [[= ::foundation::linear::borrow_match{}]] [[nodiscard]] constexpr bool is_none_or(Holds&& holds) const
        noexcept(std::is_nothrow_invocable_v<Holds&, T const&>) {
        if (!slot_.holds_()) return true;
        [[maybe_unused]] auto const lend = slot_.lend_();
        return holds(slot_.payload_());
    }

    // The total match.  on_some gets the payload, or on_none runs, and
    // the two arms give the same type.  A call with one arm does not
    // compile, so no caller forgets the empty case.  The rvalue form moves
    // the payload out first, so the Option is empty when on_some runs.  The
    // payload then ends in the match, so on_some takes it by value, and the
    // result holds no borrow: a reference or an address would dangle.
    template <class OnSome, class OnNone>
        requires std::is_invocable_v<OnSome, T&&> && std::is_invocable_v<OnNone>
              && std::same_as<std::invoke_result_t<OnSome, T&&>, std::invoke_result_t<OnNone>>
              && TakesPayloadByValue<OnSome, T> && HoldsNoBorrow<std::invoke_result_t<OnNone>>
    constexpr std::invoke_result_t<OnNone> match(OnSome&& on_some,
                                                 OnNone&& on_none) && noexcept(std::is_nothrow_invocable_v<OnSome, T&&>
                                                                               && std::is_nothrow_invocable_v<OnNone>) {
        if (!slot_.holds_()) return static_cast<OnNone&&>(on_none)();
        T taken(static_cast<T&&>(slot_.payload_()));
        slot_.clear_();
        return static_cast<OnSome&&>(on_some)(static_cast<T&&>(taken));
    }

    // An arm of the rvalue match that takes its payload by reference binds
    // to this overload, so the refusal gives its reason.
    template <class OnSome, class OnNone>
        requires std::is_invocable_v<OnSome, T&&> && std::is_invocable_v<OnNone>
                  && std::same_as<std::invoke_result_t<OnSome, T&&>, std::invoke_result_t<OnNone>>
                  && (!TakesPayloadByValue<OnSome, T>)
    void match(OnSome&& on_some, OnNone&& on_none) && = delete(
        "an arm of the rvalue match takes its payload by value, so it names no object that the match ends: write the "
        "parameter as a value, or name the Option and match its borrow");

    // A result of the rvalue match that holds a borrow binds to this
    // overload, so the refusal gives its reason.
    template <class OnSome, class OnNone>
        requires std::is_invocable_v<OnSome, T&&> && std::is_invocable_v<OnNone>
                  && std::same_as<std::invoke_result_t<OnSome, T&&>, std::invoke_result_t<OnNone>>
                  && TakesPayloadByValue<OnSome, T> && (!HoldsNoBorrow<std::invoke_result_t<OnNone>>)
    void match(OnSome&& on_some, OnNone&& on_none) && = delete(
        "the result of the rvalue match holds no borrow, because the payload ends in the match: give a value with "
        "no reference and no address, or name the Option and match its borrow");

    // The total match of a borrow: on_some gets the payload as a const
    // reference, and the Option keeps it.  The parameter of on_some is T or
    // T const&, so no conversion reads the payload as another type.  The rule
    // borrow-match of the plugin refuses an arm that assigns, moves or
    // changes this Option, and an owning Option counts the lend (the lend
    // count).
    template <class OnSome, class OnNone>
        requires std::is_invocable_v<OnSome, T const&> && std::is_invocable_v<OnNone>
              && std::same_as<std::invoke_result_t<OnSome, T const&>, std::invoke_result_t<OnNone>>
              && TakesPayloadByBorrow<OnSome, T>
    [[= ::foundation::linear::borrow_match{}]] constexpr std::invoke_result_t<OnNone>
        match(OnSome&& on_some, OnNone&& on_none) const& noexcept(std::is_nothrow_invocable_v<OnSome, T const&>
                                                                  && std::is_nothrow_invocable_v<OnNone>) {
        if (!slot_.holds_()) return static_cast<OnNone&&>(on_none)();
        [[maybe_unused]] auto const lend = slot_.lend_();
        return static_cast<OnSome&&>(on_some)(slot_.payload_());
    }

    // An arm of the borrow match that takes another type binds to this
    // overload, so the refusal gives its reason.
    template <class OnSome, class OnNone>
        requires std::is_invocable_v<OnSome, T const&> && std::is_invocable_v<OnNone>
                  && std::same_as<std::invoke_result_t<OnSome, T const&>, std::invoke_result_t<OnNone>>
                  && (!TakesPayloadByBorrow<OnSome, T>)
    void match(OnSome&& on_some, OnNone&& on_none) const& = delete(
        "an arm of the borrow match takes the payload type, as a value or a const reference, so no conversion reads "
        "the payload as another type: write the parameter as the payload type");

    // A const rvalue of an Option that the borrow match would take binds to
    // this overload, so an arm cannot give a reference into an object that the
    // full expression ends.  An rvalue that is not const binds to the rvalue
    // match.  The constraint is that of the borrow match, so a call with the
    // wrong arms gets the refusal of its arms.
    template <class OnSome, class OnNone>
        requires std::is_invocable_v<OnSome, T const&> && std::is_invocable_v<OnNone>
                  && std::same_as<std::invoke_result_t<OnSome, T const&>, std::invoke_result_t<OnNone>>
                  && TakesPayloadByBorrow<OnSome, T>
    void match(OnSome&& on_some, OnNone&& on_none) const&& = delete(
        "a borrow match of a temporary Option gives a reference into an object that the full expression ends: match "
        "the rvalue, or name the Option");

    // The frame door of the payload (foundation/core/Frame.h): on_some gets
    // the RefMut of the payload in place, or on_none runs for an empty
    // Option, and the door gives the result of the arm.  The Option keeps
    // the payload, and the door writes no byte of the Option.  The rule
    // borrow-match refuses an arm that assigns, moves or changes this
    // Option, and an owning Option counts the lend (the lend count), so no
    // arm ends the payload while it holds the RefMut.  A write through the
    // RefMut bypasses the test of Option::some, so a niche payload takes the
    // door only when its niche states that no door of T builds the empty
    // value.  The body reads the niche, as the choice of a cursor did, so no
    // constraint reads the open template niche.  The brand comes after the
    // deduced arms and a pack, so no caller names it.  One branch.
    template <class OnSome, class OnNone, class... Refused, ::foundation::brand::IsBrand Fresh = CRUCIBLE_SCOPE_BRAND>
        requires(sizeof...(Refused) == 0)
             && OptionFrameArms<T, OnSome, OnNone, Fresh> && FrameResult<std::invoke_result_t<OnNone&&>, Fresh>
    [[= ::foundation::linear::borrow_match{}]] constexpr std::invoke_result_t<OnNone&&>
        with_mut(OnSome&& on_some, OnNone&& on_none) & noexcept {
        static_assert(detail::UndeclaredNicheOf<T> || detail::NicheRead<T>::is_empty_never_built(),
                      "Option::with_mut: a write through the RefMut can write the empty value of the niche of the "
                      "payload, and a full Option would then read as empty.  State is_empty_never_built in the niche "
                      "of the payload, or match the Option and build a new one");
        if (!slot_.holds_()) return static_cast<OnNone&&>(on_none)();
        [[maybe_unused]] auto const lend = slot_.lend_();
        return static_cast<OnSome&&>(on_some)(RefMut<T, Fresh>{detail::FrameDoor::key(), slot_.payload_()});
    }

    // Arms whose result holds a borrow or names the brand of the frame bind
    // to this overload, so the refusal gives its reason.
    template <class OnSome, class OnNone, class... Refused, ::foundation::brand::IsBrand Fresh = CRUCIBLE_SCOPE_BRAND>
        requires(sizeof...(Refused) == 0)
                 && OptionFrameArms<T, OnSome, OnNone, Fresh> && (!FrameResult<std::invoke_result_t<OnNone &&>, Fresh>)
    void with_mut(OnSome&& on_some, OnNone&& on_none) & = delete(
        "the result of a frame door leaves the frame, so it holds no reference and no address, and it names no brand "
        "of the frame: give a plain value");

    template <class OnSome, class OnNone>
    void with_mut(OnSome&& on_some, OnNone&& on_none) && =
        delete("a frame door of a temporary Option writes a payload that the full expression ends: name the Option");

    // The loop `for (T const& value : option)` makes one turn when the
    // Option holds a value, and no turn when it is empty.  The loop reads
    // the payload, also of an Option that is not const.  The frame door
    // with_mut writes the payload in place (foundation/core/Frame.h).  A
    // cursor of a temporary Option would point into an object that the full
    // expression ends, so the overloads for an rvalue are deleted.  A loop
    // over a temporary binds it to a reference first, and calls these
    // overloads on that lvalue.
    //
    // The door begin() carries the annotation borrow_match, so the rule
    // borrow-match reads the body of the loop as an arm, and it refuses a
    // write of this Option in the body (the loop of the rule).  A cursor
    // counts no lend, so only a niche or a plain Option has the loop.  Those
    // two forms own nothing, and their borrow match counts no lend either.
    [[= ::foundation::linear::borrow_match{}]] [[nodiscard]] constexpr OptionCursor<T const> begin() const& noexcept
        requires(detail::option_form_of<T> != detail::OptionForm::owning)
    {
        return OptionCursor<T const>{slot_.holds_() ? &slot_.payload_() : nullptr};
    }
    [[nodiscard]] constexpr OptionCursor<T const> end() const& noexcept
        requires(detail::option_form_of<T> != detail::OptionForm::owning)
    {
        return OptionCursor<T const>{nullptr};
    }

    // An owning Option counts each lend of its payload (the lend count), and
    // a cursor counts none.  A loop would then lend the payload while a write
    // in a callee of the body ends it, with no check in any build.  So an
    // owning Option has no loop: the borrow match, is_some_and, is_none_or
    // and with_mut read its payload and count the lend.
    void begin() const&
        requires(detail::option_form_of<T> == detail::OptionForm::owning)
    = delete("a loop over an owning Option counts no lend of its payload, and a write that ends the payload in the "
             "body goes unseen: read the payload with match, is_some_and or is_none_or");
    void end() const&
        requires(detail::option_form_of<T> == detail::OptionForm::owning)
    = delete("a loop over an owning Option counts no lend of its payload, and a write that ends the payload in the "
             "body goes unseen: read the payload with match, is_some_and or is_none_or");

    void begin() && = delete("a cursor of a temporary Option points into an object that the full expression ends");
    void end() && = delete("a cursor of a temporary Option points into an object that the full expression ends");
    void begin() const&& = delete("a cursor of a temporary Option points into an object that the full expression ends");
    void end() const&& = delete("a cursor of a temporary Option points into an object that the full expression ends");

    // Two Options of one type are equal when both are empty, or when both
    // hold equal payloads.  A payload is read only when both hold one, and
    // equal of the Protocol family compares the payloads.  The cost is one
    // test of each state and one compare of the payloads.
    [[nodiscard]] friend constexpr bool operator==(Option const& left, Option const& right) noexcept
        requires Equatable<T>
    {
        return left.slot_.same_state_(right.slot_)
            && (!left.slot_.holds_() || ::foundation::core::equal(left.slot_.payload_(), right.slot_.payload_()));
    }

    // Each other operand, such as none, a payload, or an Option of a
    // different payload type, binds to this overload, so no implicit
    // constructor makes a compare of two types.
    template <class Other>
        requires(!std::same_as<Other, Option>)
    friend bool operator==(Option const&, Other const&) =
        delete("an Option compares only with an Option of its own payload type: test is_some() or is_none(), or match");
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

// The dispatch tag of the constructor that builds no alternative.  The slot
// that holds the storage builds one alternative in place in the body of its
// own constructor.
struct NoAlternativeYet final {
    explicit constexpr NoAlternativeYet() noexcept = default;
};

// The storage of a sum: one alternative alive at a time.  The union nests
// the alternatives after the first one, so each alternative is a direct
// member of the union at its depth, and a constant evaluation can follow the
// active member.  The destructor is trivial when each alternative is
// trivially destructible.  Else it does nothing, and the slot destroys the
// active alternative.  Complexity: the depth of the nest, and each walk of
// an index, are linear in the number of alternatives.  The alternatives are
// private, and only alternative_of_ reads one.  The tag of the slot names the
// active member, so the union carries the mark closed_tag, and the quarantine
// plugin reads a member of it as no copy of bytes.
template <class... Ts>
union Alternatives;

// The alternative at an index of a storage.
template <std::size_t Index, class Storage>
[[nodiscard]] constexpr auto& alternative_of_(Storage& storage) noexcept;

template <class Last>
union[[= ::foundation::core::closed_tag{}]] Alternatives<Last> {
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
    constexpr Alternatives(MoveActive, std::size_t index, Alternatives& source) noexcept { move_in_(index, source); }

    constexpr explicit Alternatives(NoAlternativeYet) noexcept {}

    constexpr ~Alternatives()
        requires std::is_trivially_destructible_v<Last>
    = default;
    constexpr ~Alternatives() {}

    constexpr void destroy_(std::size_t) noexcept { head_.~Last(); }

    // Build the alternative of the index from the same alternative of the
    // source.  No member is active before the call: the constructor calls it
    // first, and an assignment calls it after destroy_.
    constexpr void move_in_(std::size_t, Alternatives& source) noexcept {
        ::new(static_cast<void*>(&head_)) Last(static_cast<Last&&>(source.head_));
    }
};

template <class Head, class Next, class... Rest>
union[[= ::foundation::core::closed_tag{}]] Alternatives<Head, Next, Rest...> {
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

    constexpr Alternatives(MoveActive, std::size_t index, Alternatives& source) noexcept { move_in_(index, source); }

    constexpr explicit Alternatives(NoAlternativeYet) noexcept {}

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

    // Build the alternative of the index from the same alternative of the
    // source.  The tail is a union of its own, so a new tail starts its
    // lifetime, and its constructor builds the alternative inside it.
    constexpr void move_in_(std::size_t index, Alternatives& source) noexcept {
        if (index == 0) {
            ::new(static_cast<void*>(&head_)) Head(static_cast<Head&&>(source.head_));
        } else {
            ::new(static_cast<void*>(&tail_)) Alternatives<Next, Rest...>(MoveActive{}, index - 1, source.tail_);
        }
    }
};

template <std::size_t Index, class Storage>
[[nodiscard]] constexpr auto& alternative_of_(Storage& storage) noexcept {
    if constexpr (Index == 0) {
        return storage.head_;
    } else {
        return detail::alternative_of_<Index - 1>(storage.tail_);
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

// How an alternative of a plain sum fills the bytes of the storage that its
// value does not hold (SumSlotOf tells which).
enum class ZeroFill : unsigned char {
    // The alternative writes no byte after its value.
    none,
    // An empty alternative writes zero words over the full storage.
    storage,
    // An alternative that is smaller than the storage writes zero words over
    // the full storage, and then starts its value over the first bytes.
    under,
};

// An alternative of a plain sum.  An empty alternative, such as the empty
// marker of an Option or the Unit of a Result, has no value bytes.  Fill
// tells how the alternative writes the storage bytes that its value does not
// hold, so a copy of the sum reads no byte that no constructor wrote
// (SumSlotOf tells when).  Width is the alignment of the storage, at most eight bytes.
// Only the slot of a plain sum reads the value.
template <class T, std::size_t Size, std::size_t Width, ZeroFill Fill>
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
class PlainAlternative<T, Size, Width, ZeroFill::storage> {
    [[no_unique_address]] T value_;
    ZeroWords<typename WordOf<Width>::type, Size / Width> zero_{};

    template <bool IsPlain, class... Ts>
    friend class SumSlotOf;

public:
    template <class U>
    constexpr explicit PlainAlternative(U&& from) noexcept(std::is_nothrow_constructible_v<T, U&&>)
        : value_(static_cast<U&&>(from)) {}
};

// The zero words and the value share the bytes of the alternative.  The
// constructor writes the zero words over the full storage first, and then
// starts the value over the first bytes.  The value is the active member, and
// the bytes after it keep the zero words.  So a copy of the sum reads a
// written byte at each offset.  A member after the value would make GCC cut
// the storage into two scalars, and each other alternative would then split
// and rebuild its word.  With the zero words under the value, the storage
// stays one scalar, and GCC folds the zero words into the move of the value.
// No code writes the zero words after the constructor, so the union names its
// active member as the tag of a sum does, and it carries the mark closed_tag.
template <class T, std::size_t Size, std::size_t Width>
class PlainAlternative<T, Size, Width, ZeroFill::under> {
    static_assert(sizeof(T) < Size, "a value under zero words is smaller than the storage");

    union[[= ::foundation::core::closed_tag{}]] {
        ZeroWords<typename WordOf<Width>::type, Size / Width> zero_;
        T value_;
    };

    template <bool IsPlain, class... Ts>
    friend class SumSlotOf;

public:
    template <class U>
    constexpr explicit PlainAlternative(U&& from) noexcept(std::is_nothrow_constructible_v<T, U&&>) : zero_{} {
        ::new(static_cast<void*>(&value_)) T(static_cast<U&&>(from));
    }
};

// The fill of an alternative in a storage of Size bytes.  Only a sum of at
// most 16 bytes fills (SumSlotOf tells why).
template <class T, std::size_t Size, bool IsRegisterPair>
[[nodiscard]] consteval ZeroFill zero_fill_of_() noexcept {
    if constexpr (!IsRegisterPair) {
        return ZeroFill::none;
    } else if constexpr (std::is_empty_v<T>) {
        return ZeroFill::storage;
    } else if constexpr (sizeof(T) == Size) {
        return ZeroFill::none;
    } else {
        return ZeroFill::under;
    }
}

template <class T, class E>
class ResultSlot;

// The door of each reader of a sum slot, and the key of the reads of the
// slot: the tag and an alternative.  SumDoor is the one friend of the key,
// and the storage of Option, Result and OneOf calls the door.  OneOf.h
// defines OneOf, so no friend of the slot names it, and a unit that defines
// a different OneOf gets no read of a slot.  The door and the key hold no
// template, so no explicit specialization of a template reaches the key.
struct SumDoor;

class [[nodiscard]] SumKey final {
    friend struct SumDoor;

    constexpr SumKey() noexcept {}
    constexpr SumKey(SumKey const&) noexcept {}

public:
    SumKey& operator=(SumKey const&) = delete("a key of a sum slot lives for one read");
};

struct SumDoor {
    [[nodiscard]] static constexpr SumKey key() noexcept { return SumKey{}; }
};

// The slot of a plain sum: the storage and the tag of TaggedCells.  The tag
// is the index of the active alternative.  The storage keeps the size and
// the alignment of the union of the bare alternatives.
//
// The copy, the move and the end of the slot are those of its bytes, because
// each alternative has that shape.  So the ABI passes a slot of at most 16
// bytes in registers.  When each alternative assigns its bytes, the slot
// keeps the trivial assignment, and the slot is trivially copyable.  A sealed
// alternative has a user-provided assignment, and the union then has none.
// So the slot builds its cells again from a copy of the source, which is a
// copy of the bytes.  The slot is then not trivially copyable, and
// std::bit_cast builds no sum that holds a sealed alternative.
//
// The ABI of x86_64 and of aarch64 gives a sum of at most 16 bytes back in
// two registers.  GCC can then read each byte of the storage into a register,
// also when the active alternative is empty or smaller than the storage.
// -Wmaybe-uninitialized then reports a byte that no constructor wrote, for
// example the bytes after the error of two bytes in a Result of a value of
// eight bytes.  So each alternative of such a sum writes zero words over the
// storage bytes that its value does not hold (PlainAlternative).  In a
// register, such a zero is usually the zero extension of the move of the
// value.  A larger sum goes through memory, and GCC gives no such report for
// it.  Its alternatives write no byte after the value, so a clear of an
// Option of a large payload writes only its tag, as the reset of
// std::optional does.
template <class... Ts>
class SumSlotOf<true, Ts...> {
    static constexpr std::size_t storage_size_ = sizeof(Alternatives<Ts...>);
    static constexpr std::size_t word_width_ = alignof(Alternatives<Ts...>) < 8 ? alignof(Alternatives<Ts...>) : 8;
    using TagWord = std::conditional_t<word_width_ == 8, unsigned long long, unsigned char>;
    static constexpr bool is_register_pair_ = sizeof(TaggedCells<Alternatives<Ts...>, TagWord>) <= 16;
    using Storage = Alternatives<
        PlainAlternative<Ts, storage_size_, word_width_, zero_fill_of_<Ts, storage_size_, is_register_pair_>()>...>;
    static_assert(sizeof(Storage) == storage_size_ && alignof(Storage) == alignof(Alternatives<Ts...>),
                  "the zero words of an empty alternative do not change the storage");

    TaggedCells<Storage, TagWord> cells_;

public:
    // The tag and the alternative at Index, with the key that only SumDoor
    // builds.  The key is empty and comes by value, so a read keeps no key on
    // the stack.
    [[nodiscard]] constexpr std::size_t index_(SumKey /*key*/) const noexcept { return cells_.tag_; }
    [[nodiscard]] constexpr NoLend lend_(SumKey /*key*/) const noexcept { return NoLend{}; }
    // A plain sum holds no linear alternative (the end of a linear
    // alternative).
    constexpr void spend_(SumKey /*key*/) noexcept {}
    template <std::size_t Index>
    [[nodiscard]] constexpr auto& alternative_(SumKey /*key*/) noexcept {
        return detail::alternative_of_<Index>(cells_.storage_).value_;
    }
    template <std::size_t Index>
    [[nodiscard]] constexpr auto const& alternative_(SumKey /*key*/) const noexcept {
        return detail::alternative_of_<Index>(cells_.storage_).value_;
    }

    template <std::size_t Index, class U>
    constexpr SumSlotOf(AlternativeAt<Index> at,
                        U&& value) noexcept(std::is_nothrow_constructible_v<Storage, AlternativeAt<Index>, U&&>)
        : cells_{static_cast<TagWord>(Index), at, static_cast<U&&>(value)} {}

    constexpr SumSlotOf(SumSlotOf const&) noexcept = default;
    constexpr SumSlotOf(SumSlotOf&&) noexcept = default;
    constexpr SumSlotOf& operator=(SumSlotOf const&) noexcept
        requires(std::is_trivially_copy_assignable_v<Ts> && ...)
    = default;
    constexpr SumSlotOf& operator=(SumSlotOf&&) noexcept
        requires(std::is_trivially_move_assignable_v<Ts> && ...)
    = default;

    // The assignment of a slot with a sealed alternative.  The copy of the
    // source comes first, so a self assignment reads no cell that the new
    // cells replace.  An rvalue binds here too, because a move of the bytes
    // is their copy.
    constexpr SumSlotOf& operator=(SumSlotOf const& other) noexcept
        requires(!(std::is_trivially_copy_assignable_v<Ts> && ...) && (std::is_nothrow_copy_assignable_v<Ts> && ...)
                 && (std::is_trivially_copy_constructible_v<Ts> && ...))
    {
        TaggedCells<Storage, TagWord> const copied{other.cells_};
        ::new(static_cast<void*>(&cells_)) TaggedCells<Storage, TagWord>(copied);
        return *this;
    }

    ~SumSlotOf() = default;
};

// The slot of a sum with an alternative that owns something.  The slot is
// move-only.  A move builds the active alternative from the active
// alternative of the source, and the source keeps its moved-from
// alternative, which its destructor ends.  A borrow of the source then
// stays a borrow of a live object.  A move assignment drops the alternative
// that the slot holds, so a sum with a linear alternative has none.  The tag
// names one alternative, so the slot carries the mark closed_tag.  The slot
// counts its lends (the lend count), and a moved slot starts with none.
template <class... Ts>
class[[= ::foundation::core::closed_tag{}]] SumSlotOf<false, Ts...> {
    static constexpr bool tracks_end_ = tracks_alternative_ends_ && (!DroppablePayload<Ts> || ...);

    // True when the alternative at index is linear.  Complexity: linear in
    // the alternatives, at compile time for a constant index.
    [[nodiscard]] static constexpr bool is_linear_at_(std::size_t index) noexcept {
        std::size_t position = 0;
        bool is_linear = false;
        ((is_linear = is_linear || (position == index && !DroppablePayload<Ts>), ++position), ...);
        return is_linear;
    }

    Alternatives<Ts...> storage_;
    unsigned char tag_;
    mutable unsigned char lend_count_ = 0;
    [[no_unique_address]] EndMark<tracks_end_> end_mark_{};

public:
    // The tag and the alternative at Index, with the key that only SumDoor
    // builds.  The key is empty and comes by value, so a read keeps no key on
    // the stack.
    [[nodiscard]] constexpr std::size_t index_(SumKey /*key*/) const noexcept { return tag_; }
    [[nodiscard]] constexpr Lend lend_(SumKey /*key*/) const noexcept { return Lend{lend_count_}; }
    // A consumer takes the sum (the end of a linear alternative).
    constexpr void spend_(SumKey /*key*/) noexcept { end_mark_.spend(); }
    template <std::size_t Index>
    [[nodiscard]] constexpr auto& alternative_(SumKey /*key*/) noexcept {
        return detail::alternative_of_<Index>(storage_);
    }
    template <std::size_t Index>
    [[nodiscard]] constexpr auto const& alternative_(SumKey /*key*/) const noexcept {
        return detail::alternative_of_<Index>(storage_);
    }

    template <std::size_t Index, class U>
    constexpr SumSlotOf(AlternativeAt<Index> at, U&& value) noexcept(
        std::is_nothrow_constructible_v<Alternatives<Ts...>, AlternativeAt<Index>, U&&>)
        : storage_(at, static_cast<U&&>(value)), tag_{static_cast<unsigned char>(Index)} {}

    // The source keeps its moved-from alternative, which owes nothing.
    constexpr SumSlotOf(SumSlotOf&& other) noexcept
        : storage_(MoveActive{}, other.tag_, other.storage_), tag_{other.tag_} {
        other.end_mark_.spend();
    }

    // The source moves to a local first, because it can live inside the
    // alternative that this slot holds, and the end of that alternative
    // would end the source.  A self assignment then moves the alternative
    // out and back, with no test of the address.  The cost is two moves and
    // two ends of an alternative.
    constexpr SumSlotOf& operator=(SumSlotOf&& other) noexcept
        requires(DroppablePayload<Ts> && ...)
    {
        SumSlotOf taken(static_cast<SumSlotOf&&>(other));
        detail::refuse_lent_write_(lend_count_);
        storage_.destroy_(tag_);
        storage_.move_in_(taken.tag_, taken.storage_);
        tag_ = taken.tag_;
        return *this;
    }

    SumSlotOf& operator=(SumSlotOf&&)
        requires(!(DroppablePayload<Ts> && ...))
    = delete("an assignment drops the alternative that the sum holds, and a linear alternative must not be dropped: "
             "build a new sum instead");

    SumSlotOf(SumSlotOf const&) = delete("an owning sum is move-only: a copy would duplicate what an alternative owns");
    SumSlotOf& operator=(SumSlotOf const&) = delete("an owning sum is move-only");

    constexpr ~SumSlotOf() {
        detail::refuse_lent_write_(lend_count_);
        if constexpr (tracks_end_) {
            end_mark_.require_spent(is_linear_at_(tag_));
        }
        storage_.destroy_(tag_);
    }
};

// The one slot of the sums of the family.
template <class... Ts>
using SumSlot = SumSlotOf<is_plain_sum_v<Ts...>, Ts...>;

// The fatal exits of a tag that names no alternative of an Option or a
// Result.  The calls take no argument, so each inlined read of a tag adds
// only a compare and a branch to one call.  The names start with report_,
// as each cold fatal exit of the family does, so the rule codegen-shapes
// reads a call of one as a fatal path.
[[noreturn]] CRUCIBLE_COLD inline void report_forged_option_tag_() noexcept {
    fatal("Option: the tag names no alternative, so a copy of bytes made this Option");
}

[[noreturn]] CRUCIBLE_COLD inline void report_forged_result_tag_() noexcept {
    fatal("Result: the tag names no alternative, so a copy of bytes made this Result");
}

// A plain Option is the plain sum of the empty marker and the payload.  The
// tag is 0 for no value and 1 for a value, as the tag of a Result is.  The
// special members are implicit and trivial.
//
// Each constructor writes the tag 0 or 1.  But a copy of bytes over the sum,
// such as a memcpy or a union member, can write a different word.  Such a tag
// would read as a value, and two equal values could compare unequal.  So each
// read of the state compares the tag with 1, and a larger tag ends the
// process, as which() of a OneOf does.  The optimizer folds the second check
// of one tag, and the check of a tag that a constructor of the same function
// wrote.
template <class T>
class OptionSlot<T, OptionForm::plain> {
    SumSlot<NoPayload, T> sum_{AlternativeAt<0>{}, NoPayload{}};

    template <ChoicePayload U>
    friend class ::foundation::core::Option;

    [[nodiscard]] constexpr std::size_t tag_() const noexcept {
        std::size_t const tag = sum_.index_(SumDoor::key());
        if (tag > 1) [[unlikely]] {
            report_forged_option_tag_();
        }
        return tag;
    }

    [[nodiscard]] constexpr bool holds_() const noexcept { return tag_() != 0; }
    [[nodiscard]] constexpr NoLend lend_() const noexcept { return NoLend{}; }
    // The test of expect is one compare of the tag with 1.  The cold path
    // tells a forged tag from an empty Option.
    constexpr void expect_value_(Fmt<> why) const noexcept {
        std::size_t const tag = sum_.index_(SumDoor::key());
        if (tag != 1) [[unlikely]] {
            static_cast<void>(tag_());
            fatal(why);
        }
    }
    // The tag is the state, so one compare of the two checked tags tests the
    // two states, and no bool of a state is built for the compare.
    [[nodiscard]] constexpr bool same_state_(OptionSlot const& other) const noexcept { return tag_() == other.tag_(); }
    [[nodiscard]] constexpr T& payload_() noexcept { return sum_.template alternative_<1>(SumDoor::key()); }
    [[nodiscard]] constexpr T const& payload_() const noexcept { return sum_.template alternative_<1>(SumDoor::key()); }

    // A payload with no assignment, such as a closure or a class with a const
    // member, gives a slot with no assignment.  The empty marker or the next
    // payload then starts in place over the slot: each part of a plain sum
    // ends with no action, so the old slot holds nothing to end.
    static constexpr bool assigns_slot_ = std::is_move_assignable_v<SumSlot<NoPayload, T>>;

    constexpr void clear_() noexcept {
        if constexpr (assigns_slot_) {
            sum_ = SumSlot<NoPayload, T>{AlternativeAt<0>{}, NoPayload{}};
        } else {
            ::new(static_cast<void*>(&sum_)) SumSlot<NoPayload, T>{AlternativeAt<0>{}, NoPayload{}};
        }
    }

    // Builds the payload of an empty slot: the cells of the payload and the
    // tag 1, as the constructor of a full slot writes them.
    template <class U>
    constexpr void fill_(U&& value) noexcept(std::is_nothrow_constructible_v<T, U&&>) {
        if constexpr (assigns_slot_) {
            sum_ = SumSlot<NoPayload, T>{AlternativeAt<1>{}, static_cast<U&&>(value)};
        } else {
            ::new(static_cast<void*>(&sum_)) SumSlot<NoPayload, T>{AlternativeAt<1>{}, static_cast<U&&>(value)};
        }
    }

public:
    constexpr OptionSlot() noexcept = default;

    template <class U>
    constexpr OptionSlot(SomeTag, U&& value) noexcept(std::is_nothrow_constructible_v<T, U&&>)
        : sum_{AlternativeAt<1>{}, static_cast<U&&>(value)} {}
};

// An owning Option holds the alternatives of the empty marker and the
// payload, and a flag.  A move builds the payload in the new slot, and the
// source becomes empty.  A move assignment drops the payload that the slot
// holds, so a linear payload has none.  The destructor is not trivial, so no
// lifetime start over bytes builds the slot.  The annotation states that
// fact to the rule linear-forge, which reads an annotation and not a
// destructor.  The slot counts its lends (the lend count), and a moved slot
// starts with none.
//
// Zero words of an empty slot
//   The destructor of the slot reads the payload only when the flag is set.
//   Where GCC does not fold the flag, the destructor keeps a branch that reads
//   the payload.  -Wmaybe-uninitialized does no test of the flag for a read of
//   memory, so it reports that read when no store wrote those bytes.  GCC 16
//   gave that report at -O2 and -O3 in these cases:
//     - The end of an Option of a lanes producer handle of 24 bytes, after
//       expect
//     - The end of an escaped Option of a region of 104 bytes, after expect
//       and a fence
//     - The move of a payload of 112 bytes that holds an empty owning Option.
//   The late uninit pass walks past a fence or an atomic load, but value
//   numbering does not fold the flag of an escaped Option across it.
//
//   Only a destructor that is not trivial reads the bytes of a payload.  So
//   each empty state of a slot of such a payload holds a third alternative of
//   zero words: the default slot, the move of an empty source, and the slot
//   after a clear.  The empty state then has one fixed value, as the empty
//   alternative of a small plain sum has (SumSlotOf).  The slot tells its
//   state by its flag, so the third alternative is one more empty state.  A
//   payload with a trivial destructor, such as the reservation of the trace
//   ring, keeps the empty marker, which writes no byte.
template <class T>
class[[= ::foundation::lifetime::no_start_over_bytes{}]] OptionSlot<T, OptionForm::owning> {
    static constexpr std::size_t word_width_ = alignof(T) < 8 ? alignof(T) : 8;
    using EndedWords = ZeroWords<typename WordOf<word_width_>::type, sizeof(T) / word_width_>;
    using Storage = Alternatives<NoPayload, T, EndedWords>;
    // The alternative of an empty slot (the zero words of an empty slot).
    static constexpr bool writes_zero_words_ = !std::is_trivially_destructible_v<T>;
    static constexpr std::size_t empty_index_ = writes_zero_words_ ? 2 : 0;
    using EmptyAlternative = std::conditional_t<writes_zero_words_, EndedWords, NoPayload>;

    Storage storage_{AlternativeAt<empty_index_>{}, EmptyAlternative{}};
    bool is_engaged_ = false;
    mutable unsigned char lend_count_ = 0;

    template <ChoicePayload U>
    friend class ::foundation::core::Option;

    [[nodiscard]] constexpr bool holds_() const noexcept { return is_engaged_; }
    [[nodiscard]] constexpr Lend lend_() const noexcept { return Lend{lend_count_}; }
    constexpr void expect_value_(Fmt<> why) const noexcept {
        if (!is_engaged_) [[unlikely]] {
            fatal(why);
        }
    }
    [[nodiscard]] constexpr bool same_state_(OptionSlot const& other) const noexcept {
        return is_engaged_ == other.is_engaged_;
    }
    [[nodiscard]] constexpr T& payload_() noexcept { return detail::alternative_of_<1>(storage_); }
    [[nodiscard]] constexpr T const& payload_() const noexcept { return detail::alternative_of_<1>(storage_); }
    // Builds the alternative of an empty slot in place.  The storage holds no
    // live payload, so no end is due.
    constexpr void build_empty_() noexcept {
        ::new(static_cast<void*>(&storage_)) Storage(AlternativeAt<empty_index_>{}, EmptyAlternative{});
    }

    constexpr void clear_() noexcept {
        detail::refuse_lent_write_(lend_count_);
        if (is_engaged_) {
            storage_.destroy_(1);
            build_empty_();
            is_engaged_ = false;
        }
    }

    // Builds the payload of an empty slot in place.  The storage of an empty
    // slot holds the empty marker or the zero words, and no one of them needs
    // an end.  So a new storage of the payload starts over it, as the storage
    // of the constructor of a full slot.
    template <class U>
    constexpr void fill_(U&& value) noexcept(std::is_nothrow_constructible_v<T, U&&>) {
        ::new(static_cast<void*>(&storage_)) Storage(AlternativeAt<1>{}, static_cast<U&&>(value));
        is_engaged_ = true;
    }

public:
    constexpr OptionSlot() noexcept = default;

    template <class U>
    constexpr OptionSlot(SomeTag, U&& value) noexcept(std::is_nothrow_constructible_v<T, U&&>)
        : storage_(AlternativeAt<1>{}, static_cast<U&&>(value)), is_engaged_{true} {}

    // The move builds one alternative in place: the payload of a full source,
    // or a new empty alternative.  Only the flag of a source tells that it is
    // empty, so the move reads no alternative of an empty source.
    constexpr OptionSlot(OptionSlot&& other) noexcept : storage_(NoAlternativeYet{}), is_engaged_{other.is_engaged_} {
        if (is_engaged_) {
            ::new(static_cast<void*>(&storage_)) Storage(AlternativeAt<1>{}, static_cast<T&&>(other.payload_()));
        } else {
            build_empty_();
        }
        other.clear_();
    }

    // A full source moves to a local first, because it can live inside the
    // payload of this slot, and the end of that payload would end the
    // source.  A self assignment then moves the payload out and back, with
    // no test of the address.  The source is empty after the call.
    constexpr OptionSlot& operator=(OptionSlot&& other) noexcept
        requires DroppablePayload<T>
    {
        if (!other.is_engaged_) {
            clear_();
            return *this;
        }
        OptionSlot taken(static_cast<OptionSlot&&>(other));
        clear_();
        storage_.move_in_(1, taken.storage_);
        is_engaged_ = true;
        return *this;
    }

    OptionSlot& operator=(OptionSlot&&)
        requires(!DroppablePayload<T>)
    = delete("an assignment drops the payload that the Option holds, and a linear payload must not be dropped: "
             "take the payload out with take() or replace(value), or build it in an empty Option with fill(value)");

    OptionSlot(OptionSlot const&) =
        delete("an owning Option is move-only: a copy would duplicate what the payload owns");
    OptionSlot& operator=(OptionSlot const&) = delete("an owning Option is move-only");

    // End of a linear payload
    //   A linear payload leaves only through a consumer, so a full Option of
    //   a linear payload at its end would drop a value that each path must
    //   use one time.  The rule must-consume refuses such an end of a named
    //   Option and of a temporary at compile time.  It does not see an Option
    //   that a data member, a static object or a capture holds, or an end in
    //   the body of a door.  So the end of a full Option of a linear payload
    //   ends the process, in each build.  An empty one holds no payload to
    //   end.  A droppable payload keeps the end of its payload.
    constexpr ~OptionSlot() {
        detail::refuse_lent_write_(lend_count_);
        if constexpr (DroppablePayload<T>) {
            if (is_engaged_) storage_.destroy_(1);
        } else {
            if (is_engaged_) [[unlikely]] {
                detail::report_full_linear_end_();
            }
        }
    }
};

// The slot of a Result: SumSlot<E, T>, with the error at index 0 and the
// value at index 1.  A tag that is not zero then tells that a value is
// there, as the tag of a plain Option does, so a full Option and a Result
// of a value have one tag, and a test of the tag reads the same in each.
// The special members follow the sum.  Each read of the state compares the
// tag with 1, and a larger tag, which only a copy of bytes writes, ends the
// process, as in the plain Option.
template <class T, class E>
class ResultSlot {
    SumSlot<E, T> sum_;

    template <ChoicePayload U, ErrorValue F>
    friend class ::foundation::core::Result;

    [[nodiscard]] constexpr std::size_t tag_() const noexcept {
        std::size_t const tag = sum_.index_(SumDoor::key());
        if (tag > 1) [[unlikely]] {
            report_forged_result_tag_();
        }
        return tag;
    }

    [[nodiscard]] constexpr bool holds_() const noexcept { return tag_() != 0; }
    [[nodiscard]] constexpr auto lend_() const noexcept { return sum_.lend_(SumDoor::key()); }
    // The test of expect is one compare of the tag with 1.  The cold path
    // tells a forged tag from an error.
    constexpr void expect_value_(Fmt<> why) const noexcept {
        std::size_t const tag = sum_.index_(SumDoor::key());
        if (tag != 1) [[unlikely]] {
            static_cast<void>(tag_());
            fatal(why);
        }
    }
    [[nodiscard]] constexpr bool same_state_(ResultSlot const& other) const noexcept { return tag_() == other.tag_(); }
    [[nodiscard]] constexpr T& value_() noexcept { return sum_.template alternative_<1>(SumDoor::key()); }
    [[nodiscard]] constexpr T const& value_() const noexcept { return sum_.template alternative_<1>(SumDoor::key()); }
    [[nodiscard]] constexpr E& error_() noexcept { return sum_.template alternative_<0>(SumDoor::key()); }
    [[nodiscard]] constexpr E const& error_() const noexcept { return sum_.template alternative_<0>(SumDoor::key()); }
    // The value and the error for a consumer, which takes the Result.  The
    // end of the slot then reads no linear alternative as owed (the end of
    // a linear alternative).
    [[nodiscard]] constexpr T&& take_value_() noexcept {
        sum_.spend_(SumDoor::key());
        return static_cast<T&&>(value_());
    }
    [[nodiscard]] constexpr E&& take_error_() noexcept {
        sum_.spend_(SumDoor::key());
        return static_cast<E&&>(error_());
    }

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
class [[nodiscard]][[= ::foundation::linear::linear_when<HoldsLinearAlternative<T, E>>]]
                   [[= ::foundation::linear::sum{}]][[= ::foundation::lifetime::no_start_over_bytes{}]] Result {
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
        "from F, or convert the error with map_err");

    Result(NoValue) = delete("an empty Option has no error for a Result: write std::move(option).ok_or(error)");

    [[nodiscard]] constexpr bool is_ok() const noexcept { return slot_.holds_(); }
    [[nodiscard]] constexpr bool is_err() const noexcept { return !slot_.holds_(); }

    // The value as an Option, which is empty for an error.  The call drops
    // the error, so a linear error has no ok().
    [[nodiscard]] constexpr Option<T> ok() && noexcept
        requires DroppablePayload<E>
    {
        if (!slot_.holds_()) return Option<T>{none};
        return Option<T>::some(slot_.take_value_());
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
    // resource back once.  The call drops a value, so a linear value has
    // no err() of a linear error: match takes both.
    [[nodiscard]] constexpr Option<E> err() && noexcept
        requires(!DroppablePayload<E> && DroppablePayload<T>)
    {
        if (slot_.holds_()) return Option<E>{none};
        return Option<E>::some(slot_.take_error_());
    }

    // The value, or fallback for an error.  The call drops the fallback or
    // the error, so a linear value or error has no value_or().
    [[nodiscard]] constexpr T value_or(T fallback) && noexcept
        requires(DroppablePayload<T> && DroppablePayload<E>)
    {
        if (!slot_.holds_()) return fallback;
        return slot_.take_value_();
    }

    // The fatal unwrap at a boundary.  The reason tells why the operation
    // cannot fail there.  An error gives the reason and the place of the
    // call to fatal(), and the process ends.  A Result of Unit gives no
    // value, so its expect() gives void and the caller discards nothing.
    [[nodiscard]] constexpr T expect(Fmt<> why) && noexcept
        requires(!std::same_as<T, Unit>)
    {
        slot_.expect_value_(why);
        return slot_.take_value_();
    }

    constexpr void expect(Fmt<> why) && noexcept
        requires std::same_as<T, Unit>
    {
        slot_.expect_value_(why);
    }

    // Combinators
    //   A combinator is the rvalue match with one arm fixed by the shape of
    //   the sum.  map and and_then pass the error through, and map_err and
    //   or_else pass the value through.  Each consumes the Result through &&,
    //   fn takes its part by value, and the result of fn holds no borrow, by
    //   the gates of the rvalue match.  A linear part moves into fn or into the
    //   new Result, so no combinator drops a part.  is_ok_and and is_err_and
    //   only read, through a const reference.  Each body tests the error
    //   first, as the match does, and costs the one branch of the match.

    // The result of fn on the value, with the same error, or the error.
    template <class Fn>
        requires std::is_invocable_v<Fn, T&&> && TakesPayloadByValue<Fn, T>
              && ChoicePayload<std::invoke_result_t<Fn, T&&>> && HoldsNoBorrow<std::invoke_result_t<Fn, T&&>>
    [[nodiscard]] constexpr Result<std::invoke_result_t<Fn, T&&>, E>
    map(Fn&& fn) && noexcept(std::is_nothrow_invocable_v<Fn, T&&>) {
        using Mapped = Result<std::invoke_result_t<Fn, T&&>, E>;
        if (!slot_.holds_()) return Mapped{Err<E>{slot_.take_error_()}};
        return Mapped{static_cast<Fn&&>(fn)(slot_.take_value_())};
    }

    // An fn of map that takes its value by reference binds to this overload,
    // so the refusal gives its reason.
    template <class Fn>
        requires std::is_invocable_v<Fn, T&&> && (!TakesPayloadByValue<Fn, T>)
    void map(Fn&& fn) && = delete(
        "the function of map takes the value by value, so it names no object that map ends: write the parameter as a "
        "value, or name the Result and match its borrow");

    // The value, or the result of fn on the error.  The new error is an
    // ErrorValue, so a plain integer is refused, as for each Result.
    template <class Fn>
        requires std::is_invocable_v<Fn, E&&> && TakesPayloadByValue<Fn, E>
              && HoldsNoBorrow<std::invoke_result_t<Fn, E&&>> && ErrorValue<std::invoke_result_t<Fn, E&&>>
    [[nodiscard]] constexpr Result<T, std::invoke_result_t<Fn, E&&>>
    map_err(Fn&& fn) && noexcept(std::is_nothrow_invocable_v<Fn, E&&>) {
        using Failed = std::invoke_result_t<Fn, E&&>;
        using Mapped = Result<T, Failed>;
        if (!slot_.holds_()) return Mapped{Err<Failed>{static_cast<Fn&&>(fn)(slot_.take_error_())}};
        return Mapped{slot_.take_value_()};
    }

    // An fn of map_err that takes its error by reference binds to this
    // overload, so the refusal gives its reason.
    template <class Fn>
        requires std::is_invocable_v<Fn, E&&> && (!TakesPayloadByValue<Fn, E>)
    void map_err(Fn&& fn) && =
        delete("the function of map_err takes the error by value, so it names no object that map_err ends: write the "
               "parameter as a value, or name the Result and match its borrow");

    // The Result that fn gives for the value, or the error.  The error
    // converts to the error of that Result as CRUCIBLE_TRY converts it.
    template <class Fn>
        requires std::is_invocable_v<Fn, T&&> && TakesPayloadByValue<Fn, T>
              && detail::IsResult<std::invoke_result_t<Fn, T&&>> && HoldsNoBorrow<std::invoke_result_t<Fn, T&&>>
              && std::is_nothrow_constructible_v<std::invoke_result_t<Fn, T&&>, Err<E>>
    [[nodiscard]] constexpr std::invoke_result_t<Fn, T&&>
    and_then(Fn&& fn) && noexcept(std::is_nothrow_invocable_v<Fn, T&&>) {
        if (!slot_.holds_()) return Err<E>{slot_.take_error_()};
        return static_cast<Fn&&>(fn)(slot_.take_value_());
    }

    // An fn of and_then that takes its value by reference binds to this
    // overload, so the refusal gives its reason.
    template <class Fn>
        requires std::is_invocable_v<Fn, T&&> && (!TakesPayloadByValue<Fn, T>)
    void and_then(Fn&& fn) && =
        delete("the function of and_then takes the value by value, so it names no object that and_then ends: write the "
               "parameter as a value, or name the Result and match its borrow");

    // The value, or the Result that fn gives for the error: a recovery.  fn
    // gives a Result of the same value type, so the value passes with no
    // conversion.
    template <class Fn>
        requires std::is_invocable_v<Fn, E&&> && TakesPayloadByValue<Fn, E>
              && detail::IsResult<std::invoke_result_t<Fn, E&&>> && HoldsNoBorrow<std::invoke_result_t<Fn, E&&>>
              && std::same_as<detail::result_value_t<std::invoke_result_t<Fn, E&&>>, T>
    [[nodiscard]] constexpr std::invoke_result_t<Fn, E&&>
    or_else(Fn&& fn) && noexcept(std::is_nothrow_invocable_v<Fn, E&&>) {
        using Recovered = std::invoke_result_t<Fn, E&&>;
        if (!slot_.holds_()) return static_cast<Fn&&>(fn)(slot_.take_error_());
        return Recovered{slot_.take_value_()};
    }

    // An fn of or_else that takes its error by reference binds to this
    // overload, so the refusal gives its reason.
    template <class Fn>
        requires std::is_invocable_v<Fn, E&&> && (!TakesPayloadByValue<Fn, E>)
    void or_else(Fn&& fn) && =
        delete("the function of or_else takes the error by value, so it names no object that or_else ends: write the "
               "parameter as a value, or name the Result and match its borrow");

    // True when the Result holds a value that holds accepts.  holds reads the
    // value through a const reference, the Result keeps it, and an owning
    // Result counts the lend (the lend count), as the borrow match does.
    template <class Holds>
        requires std::is_invocable_v<Holds&, T const&> && std::same_as<std::invoke_result_t<Holds&, T const&>, bool>
    [[= ::foundation::linear::borrow_match{}]] [[nodiscard]] constexpr bool is_ok_and(Holds&& holds) const
        noexcept(std::is_nothrow_invocable_v<Holds&, T const&>) {
        [[maybe_unused]] auto const lend = slot_.lend_();
        if (!slot_.holds_()) return false;
        return holds(slot_.value_());
    }

    // True when the Result holds an error that holds accepts.  The read is
    // that of is_ok_and.
    template <class Holds>
        requires std::is_invocable_v<Holds&, E const&> && std::same_as<std::invoke_result_t<Holds&, E const&>, bool>
    [[= ::foundation::linear::borrow_match{}]] [[nodiscard]] constexpr bool is_err_and(Holds&& holds) const
        noexcept(std::is_nothrow_invocable_v<Holds&, E const&>) {
        [[maybe_unused]] auto const lend = slot_.lend_();
        if (slot_.holds_()) return false;
        return holds(slot_.error_());
    }

    // The total match: on_ok gets the value, or on_err gets the error, and
    // the two arms give the same type.  A call with one arm does not
    // compile, so no caller forgets the error.  The rvalue form gives up
    // the Result, which is often a temporary, so each arm takes its part by
    // value, and the result holds no borrow: a reference or an address would
    // dangle.  A linear part moves into its arm.
    template <class OnOk, class OnErr>
        requires std::is_invocable_v<OnOk, T&&> && std::is_invocable_v<OnErr, E>
              && std::same_as<std::invoke_result_t<OnOk, T&&>, std::invoke_result_t<OnErr, E>>
              && TakesPayloadByValue<OnOk, T> && TakesPayloadByValue<OnErr, E>
              && HoldsNoBorrow<std::invoke_result_t<OnErr, E>>
    constexpr std::invoke_result_t<OnErr, E>
    match(OnOk&& on_ok,
          OnErr&& on_err) && noexcept(std::is_nothrow_invocable_v<OnOk, T&&> && std::is_nothrow_invocable_v<OnErr, E>) {
        if (!slot_.holds_()) return static_cast<OnErr&&>(on_err)(slot_.take_error_());
        return static_cast<OnOk&&>(on_ok)(slot_.take_value_());
    }

    // An arm of the rvalue match that takes its part by reference binds to
    // this overload, so the refusal gives its reason.
    template <class OnOk, class OnErr>
        requires std::is_invocable_v<OnOk, T&&> && std::is_invocable_v<OnErr, E>
                  && std::same_as<std::invoke_result_t<OnOk, T&&>, std::invoke_result_t<OnErr, E>>
                  && (!(TakesPayloadByValue<OnOk, T> && TakesPayloadByValue<OnErr, E>))
    void match(OnOk&& on_ok, OnErr&& on_err) && = delete(
        "an arm of the rvalue match takes its part by value, so it names no object that the match ends: write the "
        "parameter as a value, or name the Result and match its borrow");

    // A result of the rvalue match that holds a borrow binds to this
    // overload, so the refusal gives its reason.
    template <class OnOk, class OnErr>
        requires std::is_invocable_v<OnOk, T&&> && std::is_invocable_v<OnErr, E>
                  && std::same_as<std::invoke_result_t<OnOk, T&&>, std::invoke_result_t<OnErr, E>>
                  && TakesPayloadByValue<OnOk, T> && TakesPayloadByValue<OnErr, E>
                  && (!HoldsNoBorrow<std::invoke_result_t<OnErr, E>>)
    void match(OnOk&& on_ok, OnErr&& on_err) && = delete(
        "the result of the rvalue match holds no borrow, because each part ends in its arm: give a value with no "
        "reference and no address, or name the Result and match its borrow");

    // The total match of a borrow: on_ok gets the value and on_err gets the
    // error, each as a const reference, and the Result keeps them.  The
    // parameter of each arm is its part type or a const reference to it, so
    // no conversion reads a part as another type.  The rule borrow-match of
    // the plugin refuses an arm that assigns, moves or changes this Result,
    // and an owning Result counts the lend (the lend count).
    template <class OnOk, class OnErr>
        requires std::is_invocable_v<OnOk, T const&> && std::is_invocable_v<OnErr, E const&>
              && std::same_as<std::invoke_result_t<OnOk, T const&>, std::invoke_result_t<OnErr, E const&>>
              && TakesPayloadByBorrow<OnOk, T> && TakesPayloadByBorrow<OnErr, E>
    [[= ::foundation::linear::borrow_match{}]] constexpr std::invoke_result_t<OnErr, E const&>
        match(OnOk&& on_ok, OnErr&& on_err) const& noexcept(std::is_nothrow_invocable_v<OnOk, T const&>
                                                            && std::is_nothrow_invocable_v<OnErr, E const&>) {
        [[maybe_unused]] auto const lend = slot_.lend_();
        if (!slot_.holds_()) return static_cast<OnErr&&>(on_err)(slot_.error_());
        return static_cast<OnOk&&>(on_ok)(slot_.value_());
    }

    // An arm of the borrow match that takes another type binds to this
    // overload, so the refusal gives its reason.
    template <class OnOk, class OnErr>
        requires std::is_invocable_v<OnOk, T const&> && std::is_invocable_v<OnErr, E const&>
                  && std::same_as<std::invoke_result_t<OnOk, T const&>, std::invoke_result_t<OnErr, E const&>>
                  && (!(TakesPayloadByBorrow<OnOk, T> && TakesPayloadByBorrow<OnErr, E>))
    void match(OnOk&& on_ok, OnErr&& on_err) const& = delete(
        "an arm of the borrow match takes its part type, as a value or a const reference, so no conversion reads a "
        "part as another type: write the parameter as the part type");

    // A const rvalue of a Result that the borrow match would take binds to
    // this overload, so an arm cannot give a reference into an object that the
    // full expression ends.  An rvalue that is not const binds to the rvalue
    // match.  The constraint is that of the borrow match, so a call with the
    // wrong arms gets the refusal of its arms.
    template <class OnOk, class OnErr>
        requires std::is_invocable_v<OnOk, T const&> && std::is_invocable_v<OnErr, E const&>
                  && std::same_as<std::invoke_result_t<OnOk, T const&>, std::invoke_result_t<OnErr, E const&>>
                  && TakesPayloadByBorrow<OnOk, T> && TakesPayloadByBorrow<OnErr, E>
    void match(OnOk&& on_ok, OnErr&& on_err) const&& = delete(
        "a borrow match of a temporary Result gives a reference into an object that the full expression ends: match "
        "the rvalue, or name the Result");

    // The frame door of the value and the error (foundation/core/Frame.h):
    // on_ok gets the RefMut of the value in place, or on_err the RefMut of
    // the error, and the door gives the result of the arm.  The Result keeps
    // its part, and the door writes no byte of the Result.  The rule
    // borrow-match refuses an arm that assigns, moves or changes this
    // Result, and an owning Result counts the lend (the lend count).  One
    // branch.
    template <class OnOk, class OnErr, class... Refused, ::foundation::brand::IsBrand Fresh = CRUCIBLE_SCOPE_BRAND>
        requires(sizeof...(Refused) == 0) && ResultFrameArms<T, E, OnOk, OnErr, Fresh>
             && FrameResult<std::invoke_result_t<OnErr&&, RefMut<E, Fresh>>, Fresh>
    [[= ::foundation::linear::borrow_match{}]] constexpr std::invoke_result_t<OnErr&&, RefMut<E, Fresh>>
        with_mut(OnOk&& on_ok, OnErr&& on_err) & noexcept {
        [[maybe_unused]] auto const lend = slot_.lend_();
        if (!slot_.holds_())
            return static_cast<OnErr&&>(on_err)(RefMut<E, Fresh>{detail::FrameDoor::key(), slot_.error_()});
        return static_cast<OnOk&&>(on_ok)(RefMut<T, Fresh>{detail::FrameDoor::key(), slot_.value_()});
    }

    // Arms whose result holds a borrow or names the brand of the frame bind
    // to this overload, so the refusal gives its reason.
    template <class OnOk, class OnErr, class... Refused, ::foundation::brand::IsBrand Fresh = CRUCIBLE_SCOPE_BRAND>
        requires(sizeof...(Refused) == 0) && ResultFrameArms<T, E, OnOk, OnErr, Fresh>
                 && (!FrameResult<std::invoke_result_t<OnErr &&, RefMut<E, Fresh>>, Fresh>)
    void with_mut(OnOk&& on_ok, OnErr&& on_err) & = delete(
        "the result of a frame door leaves the frame, so it holds no reference and no address, and it names no brand "
        "of the frame: give a plain value");

    template <class OnOk, class OnErr>
    void with_mut(OnOk&& on_ok, OnErr&& on_err) && =
        delete("a frame door of a temporary Result writes a part that the full expression ends: name the Result");

    // Two Results of one type are equal when both hold equal values, or
    // when both hold equal errors.  equal of the Protocol family compares
    // the active parts.  The cost is one test of each state and one compare
    // of the active parts.
    [[nodiscard]] friend constexpr bool operator==(Result const& left, Result const& right) noexcept
        requires Equatable<T> && Equatable<E>
    {
        if (!left.slot_.same_state_(right.slot_)) return false;
        if (left.slot_.holds_()) return ::foundation::core::equal(left.slot_.value_(), right.slot_.value_());
        return ::foundation::core::equal(left.slot_.error_(), right.slot_.error_());
    }

    // Each other operand, such as a value, an Err, or a Result of different
    // types, binds to this overload, so no implicit constructor makes a
    // compare of two types.
    template <class Other>
        requires(!std::same_as<Other, Result>)
    friend bool operator==(Result const&, Other const&) = delete(
        "a Result compares only with a Result of its own value and error types: test is_ok() or is_err(), or match");
};

namespace detail {

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

// The Err of the error of a failed Result.  A droppable value goes through
// err(), which drops the value that the Result does not hold.  A linear value
// has no such err(), so the rvalue match gives the error.  The arm of the
// value ends the process, because the operand holds an error.
template <class T, class E>
[[nodiscard]] constexpr Err<E> try_error_(Result<T, E>&& failed) noexcept {
    if constexpr (DroppablePayload<T>) {
        return ::foundation::core::err(
            static_cast<Result<T, E>&&>(failed).err().expect("CRUCIBLE_TRY tested the error"));
    } else {
        return static_cast<Result<T, E>&&>(failed).match(
            [](T) noexcept -> Err<E> { unreachable(); },
            [](E error) noexcept -> Err<E> { return Err<E>{static_cast<E&&>(error)}; });
    }
}

// The return of a failed operand: none for an Option, and the Err of the
// error for a Result.  The return statement of the macro converts it.  A
// failed Option holds no value, and a linear Option ends at expect_none,
// because the rule must-consume does not read the test of try_failed_.
template <class Hold>
[[nodiscard]] constexpr auto try_propagate_(Hold&& hold) noexcept {
    using Choice = std::remove_cvref_t<Hold>;
    if constexpr (!TryOperand<Hold&&>) {
        return TryRefused{};
    } else if constexpr (IsOption<Choice>) {
        if constexpr (::foundation::linear::is_linear_type(^^Choice)) {
            static_cast<Choice&&>(hold).expect_none("CRUCIBLE_TRY tested that the Option holds no value");
        } else {
            static_cast<void>(hold);
        }
        return none;
    } else {
        return detail::try_error_(static_cast<Choice&&>(hold));
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
