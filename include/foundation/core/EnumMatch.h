#pragma once

// The exhaustive match over the enumerators of an enumeration.
//
//   match_enum(value, arms...)  the result of the arm that names value
//   arm<E::a, E::b>(body)       an arm that names the enumerators a and b
//   arm<kSet, E::c>(body)       an arm that names each enumerator of the
//                               stated set kSet, and c
//   otherwise(body)             the arm of an open enumeration that takes
//                               each value that no other arm names
//   enumerators<E::a, E::b>     a stated set of enumerators of E, such as
//                               the elementwise kernels of a kernel kind
//   are_disjoint<kA, kB>()      true when no two of the sets of one
//                               enumeration name one enumerator
//
// Each arm names its enumerators as template arguments, and the build
// refuses a match that leaves an enumerator of a closed enumeration with no
// arm, or that names one value two times.  So a new enumerator breaks each
// match over its enumeration, until an arm or a stated set takes it.  A set
// is a list, and no default: a new enumerator is in no set.  The message of
// a refusal names the enumerator.
//
// A closed enumeration has no arm otherwise.  An open enumeration
// (OpenEnumeration of foundation/core/Scalar.h) needs one, because a value
// can name no enumerator: the arm otherwise gets each value that no other
// arm names, as the wildcard arm of a match of Rust does.
//
// The body of an arm takes no argument, and the body of the arm otherwise
// takes the value.  The match gives the result of the body that runs, and
// each body gives one result type.  A body holds no linear value by value
// (HoldsNoLinearValue of foundation/Linear.h): the match calls it, and a
// call is no consume.
//
// A value of a closed enumeration that names no enumerator ends the process
// through unreachable().  Such a value comes only from a forged cast: the
// quarantine plugin refuses a cast of a number into a closed enumeration
// outside the base, and decode and enum_from give no such value.
//
// The code.  A case label cannot stand in an expansion statement, so the
// match has two steps in one function body.  The first step is one chain of
// compares, one for each named value, that finds the position of the arm.
// The second step calls the body of that arm, so each body has one copy.
// The jump threading of GCC joins the two steps, so each compare of the
// chain goes to its body.  A body at each value of a stated set made one
// copy of the body for each value, also at -O3, because the tail merge
// keeps no loop.  A helper function for each arm would make a switch of
// each arm alone, so the chain stays in one body.  The cost: a small map of
// constants, such as four kinds to four numbers, becomes a decision tree of
// two or three compares, where a hand switch reads a table of values.  So a
// hot path keeps a hand switch that names each enumerator.  Each check is
// at compile time and linear in the named values, times the logarithm of
// the enumerators.

#include <foundation/Linear.h>
#include <foundation/core/Fatal.h>
#include <foundation/core/Scalar.h>
#include <foundation/reflect/EnumName.h>

#include <concepts>
#include <cstddef>
#include <meta>
#include <type_traits>
#include <utility>

namespace foundation::core {

// A stated set of enumerators of E: each value names an enumerator, one
// time.  The type is structural, so an arm takes a set as a template
// argument.  enumerators makes each set.
template <Enumerated E, std::size_t Count>
    requires(Count > 0)
struct Enumerators final {
    using Enum = E;
    static constexpr std::size_t count = Count;
    E values[Count] = {};
};

namespace detail {

// True when each value names an enumerator of E and no value comes two
// times.  Quadratic in the values, at compile time only.
template <class E, std::size_t Count>
[[nodiscard]] consteval bool are_distinct_enumerators_(E const (&values)[Count]) {
    for (std::size_t index = 0; index < Count; ++index) {
        if (!names_enumerator<E>(static_cast<std::underlying_type_t<E>>(values[index]))) return false;
        for (std::size_t later = index + 1; later < Count; ++later) {
            if (values[index] == values[later]) return false;
        }
    }
    return true;
}

// The enumeration of one template argument of an arm: an enumerator, or a
// stated set of enumerators.
template <auto Named>
struct named_traits_ {
    static constexpr bool is_named = false;
};

template <auto Named>
    requires Enumerated<decltype(Named)>
struct named_traits_<Named> {
    static constexpr bool is_named = true;
    using Enum = decltype(Named);
    static constexpr std::size_t count = 1;
};

template <class E, std::size_t Count, Enumerators<E, Count> Named>
struct named_traits_<Named> {
    static constexpr bool is_named = true;
    using Enum = E;
    static constexpr std::size_t count = Count;
};

// True when each template argument of an arm is an enumerator of one
// enumeration.  Such an arm, the usual one, instantiates no class for each
// value.
template <auto First, auto... Rest>
inline constexpr bool is_enumerator_pack_ =
    Enumerated<decltype(First)> && (std::same_as<decltype(First), decltype(Rest)> && ...);

// True when each template argument of an arm is an enumerator or a stated
// set, all of one enumeration.
template <auto First, auto... Rest>
inline constexpr bool is_named_pack_ =
    named_traits_<First>::is_named && (named_traits_<Rest>::is_named && ...)
    && (std::same_as<typename named_traits_<First>::Enum, typename named_traits_<Rest>::Enum> && ...);

template <class Set>
struct set_enum_ {
    using type = typename Set::Enum;
};

// The enumeration of the first template argument of an arm.
template <auto First>
using named_enum_t = typename std::conditional_t<std::is_enum_v<decltype(First)>, std::type_identity<decltype(First)>,
                                                 set_enum_<decltype(First)>>::type;

// The count of the values that the template arguments of an arm name.
template <auto First, auto... Rest>
[[nodiscard]] consteval std::size_t named_count_() {
    if constexpr (is_enumerator_pack_<First, Rest...>) {
        return 1 + sizeof...(Rest);
    } else {
        return (named_traits_<First>::count + ... + named_traits_<Rest>::count);
    }
}

// The set of First and Rest, after the check that each names an enumerator
// one time.  The check is a static_assert of the body, so the gate of
// enumerators reads only concepts, and no unit can change what it admits.
template <auto First, auto... Rest>
[[nodiscard]] consteval Enumerators<decltype(First), 1 + sizeof...(Rest)> make_enumerators_() {
    static_assert(are_distinct_enumerators_<decltype(First), 1 + sizeof...(Rest)>({First, Rest...}),
                  "enumerators: a stated set names each enumerator of its enumeration one time");
    return Enumerators<decltype(First), 1 + sizeof...(Rest)>{{First, Rest...}};
}

}  // namespace detail

// The set of the enumerators First and Rest, of one enumeration.  Each
// names an enumerator, one time.
template <auto First, auto... Rest>
    requires Enumerated<decltype(First)> && (std::same_as<decltype(First), decltype(Rest)> && ...)
inline constexpr Enumerators<decltype(First), 1 + sizeof...(Rest)> enumerators =
    detail::make_enumerators_<First, Rest...>();

// True when no two of the sets name one enumerator.  The check file of an
// enumeration proves it for the sets that live beside the enumeration.
// Quadratic in the values of the sets, at compile time only.
template <auto First, auto... Rest>
    requires(std::same_as<typename decltype(First)::Enum, typename decltype(Rest)::Enum> && ...)
[[nodiscard]] consteval bool are_disjoint() {
    using E = typename decltype(First)::Enum;
    constexpr std::size_t total = decltype(First)::count + (decltype(Rest)::count + ... + 0);
    E values[total] = {};
    std::size_t at = 0;
    for (E const value : First.values)
        values[at++] = value;
    (
        [&] {
            for (E const value : Rest.values)
                values[at++] = value;
        }(),
        ...);
    return detail::are_distinct_enumerators_<E, total>(values);
}

// An arm of match_enum: the values that it names, and its body.  arm makes
// it.
template <class Body, auto... Named>
struct EnumArm final {
    Body body;
};

// The arm of an open enumeration for each value that no other arm names.
// otherwise makes it.
template <class Body>
struct OtherArm final {
    Body body;
};

namespace detail {

template <class T>
struct arm_traits_ {
    static constexpr bool is_arm = false;
};

// The values that an arm names.  An expansion statement walks the array.
template <class E, std::size_t Count>
struct NamedValues final {
    E items[Count] = {};
};

// Puts the values of one template argument of an arm at the cursor.
template <auto Named, class E, std::size_t Count>
consteval void put_named_(NamedValues<E, Count>& values, std::size_t& at) {
    if constexpr (std::same_as<decltype(Named), E>) {
        values.items[at++] = Named;
    } else {
        for (E const value : Named.values)
            values.items[at++] = value;
    }
}

// The values of the template arguments of an arm, in order, each stated set
// in its place.  An arm of enumerators alone takes its pack as it is, so it
// instantiates no function for each value.  Linear in the values.
template <class E, std::size_t Count, auto... Named>
[[nodiscard]] consteval NamedValues<E, Count> make_named_values_() {
    if constexpr ((std::same_as<decltype(Named), E> && ...)) {
        return NamedValues<E, Count>{{Named...}};
    } else {
        NamedValues<E, Count> values{};
        std::size_t at = 0;
        (put_named_<Named>(values, at), ...);
        return values;
    }
}

// The kind of an arm and its enumeration, which the gate ArmOf reads.  The
// gate reads no list of values, so it reads no function template that a
// unit can specialize.
template <class Body, auto First, auto... Rest>
struct arm_traits_<EnumArm<Body, First, Rest...>> {
    using Enum = named_enum_t<First>;
    static constexpr bool is_arm = true;
    static constexpr bool is_other = false;
};

template <class Body>
struct arm_traits_<OtherArm<Body>> {
    static constexpr bool is_arm = true;
    static constexpr bool is_other = true;
};

// The values that an arm names, with each stated set in its place.  The
// match refuses a value that the arms name two times, also two times in one
// arm, so a set and an enumerator of one arm cannot name one value.  The
// bodies of the match read them, and no gate does.
template <class Arm>
struct arm_named_;

template <class Body, auto First, auto... Rest>
struct arm_named_<EnumArm<Body, First, Rest...>> {
    using Enum = named_enum_t<First>;
    static constexpr std::size_t count = named_count_<First, Rest...>();
    static constexpr NamedValues<Enum, count> values = make_named_values_<Enum, count, First, Rest...>();
};

// The arms of a match over E: each is an arm of E or the arm otherwise, and
// its body holds no linear value by value, also for an arm that a caller
// builds with no call of arm or otherwise.
template <class E, class Arm>
concept ArmOf = arm_traits_<Arm>::is_arm && ::foundation::linear::HoldsNoLinearValue<decltype(Arm::body)>
             && (arm_traits_<Arm>::is_other || std::same_as<typename arm_traits_<Arm>::Enum, E>);

// The positions 0 to Count - 1.  An expansion statement walks the array.
template <std::size_t Count>
struct ArmPositions final {
    std::size_t items[Count] = {};
};

template <std::size_t Count>
inline constexpr ArmPositions<Count> arm_positions_ = [] consteval {
    ArmPositions<Count> positions{};
    for (std::size_t index = 0; index < Count; ++index)
        positions.items[index] = index;
    return positions;
}();

// The position of raw in the sorted values of the enumerators of E, or the
// count of the values when no enumerator has the value raw.  Logarithmic in
// the enumerators.
template <class E>
[[nodiscard]] consteval std::size_t enumerator_position_(std::underlying_type_t<E> raw) {
    EnumeratorSet<E> const& set = enumerator_set<E>;
    std::size_t first = 0;
    std::size_t last = set.count;
    while (first < last) {
        std::size_t const middle = first + (last - first) / 2;
        if (set.sorted[middle] < raw) {
            first = middle + 1;
        } else {
            last = middle;
        }
    }
    return first < set.count && set.sorted[first] == raw ? first : set.count;
}

// The positions of the first value that no arm names and of the first value
// that the arms name two times, or the count of the values for none, and
// the count of the arms otherwise.
struct Coverage final {
    std::size_t missing = 0;
    std::size_t twice = 0;
    std::size_t count = 0;
    std::size_t others = 0;
};

template <class E, class Arm>
consteval void count_arm_(std::size_t (&counts)[EnumeratorSet<E>::declared + 1], std::size_t& others) {
    if constexpr (arm_traits_<Arm>::is_other) {
        ++others;
    } else {
        for (E const value : arm_named_<Arm>::values.items)
            ++counts[enumerator_position_<E>(static_cast<std::underlying_type_t<E>>(value))];
    }
}

// The coverage of the values of E by the arms.  Linear in the named values
// times the logarithm of the enumerators, and linear in the enumerators.
template <class E, class... Arms>
[[nodiscard]] consteval Coverage coverage_() {
    EnumeratorSet<E> const& set = enumerator_set<E>;
    std::size_t counts[EnumeratorSet<E>::declared + 1] = {};
    std::size_t others = 0;
    (count_arm_<E, Arms>(counts, others), ...);
    Coverage coverage{set.count, set.count, set.count, others};
    for (std::size_t position = set.count; position-- > 0;) {
        if (counts[position] == 0) coverage.missing = position;
        if (counts[position] > 1) coverage.twice = position;
    }
    return coverage;
}

// The text of a refusal of the build, which a static_assert prints.
struct MatchMessage final {
    char text[256] = {};
    std::size_t length = 0;

    constexpr void put(char const* part) noexcept {
        for (; *part != '\0' && length + 1 < sizeof(text); ++part)
            text[length++] = *part;
    }
    constexpr void put(::foundation::reflect::detail::NameChars part) noexcept {
        for (std::size_t index = 0; index < part.size && length + 1 < sizeof(text); ++index)
            text[length++] = part.first[index];
    }
    [[nodiscard]] constexpr char const* data() const noexcept { return text; }
    [[nodiscard]] constexpr std::size_t size() const noexcept { return length; }
};

// The message that names the enumerator at position in the sorted values of
// E.  Linear in the enumerators, and evaluated only for a refusal.
template <class E>
[[nodiscard]] consteval MatchMessage message_at_(char const* reason, std::size_t position) {
    MatchMessage message{};
    message.put(reason);
    auto const raw = enumerator_set<E>.sorted[position];
    for (std::meta::info const enumerator : std::meta::enumerators_of(^^E)) {
        if (static_cast<std::underlying_type_t<E>>(std::meta::extract<E>(enumerator)) != raw) continue;
        message.put(::foundation::reflect::detail::read_identifier_chars(enumerator));
        break;
    }
    message.put(" of ");
    message.put(::foundation::reflect::detail::read_identifier_chars(^^E));
    return message;
}

// The position of the arm otherwise, or the count of the arms.
template <class... Arms>
[[nodiscard]] consteval std::size_t other_position_() {
    std::size_t position = sizeof...(Arms);
    std::size_t at = 0;
    ((arm_traits_<Arms>::is_other ? (position = at, ++at) : ++at), ...);
    return position;
}

// The result of the body of an arm, and whether the call can throw.  The
// call expressions are read directly, because the invoke traits of the
// standard library cost more instantiations for each arm.
template <class E, class Arm>
struct arm_result_ {
    using type = decltype(std::declval<decltype(Arm::body)&>()());
};

template <class E, class Body>
struct arm_result_<E, OtherArm<Body>> {
    using type = decltype(std::declval<Body&>()(std::declval<E>()));
};

template <class E, class Arm>
inline constexpr bool is_nothrow_arm_ = noexcept(std::declval<decltype(Arm::body)&>()());

template <class E, class Body>
inline constexpr bool is_nothrow_arm_<E, OtherArm<Body>> = noexcept(std::declval<Body&>()(std::declval<E>()));

// The two steps of the match (the code, above): the chain of compares, one
// for each named value, finds the position of the arm, and the second step
// calls the body at that position.  A value that no arm names goes to the
// arm otherwise, or ends the process.  Linear in the named values.
template <class Result, class E, class... Arms>
[[nodiscard]] constexpr Result run_arms_(E value, Arms&... arms) noexcept((is_nothrow_arm_<E, Arms> && ...)) {
// An expansion statement unrolls into successive scopes that each declare
// the same induction variable, so -Wshadow fires once for each iteration.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    constexpr std::size_t none = sizeof...(Arms);
    std::size_t hit = none;
    template for (constexpr std::size_t at : arm_positions_<sizeof...(Arms)>.items) {
        using Arm = Arms...[at];
        if constexpr (!arm_traits_<Arm>::is_other) {
            template for (constexpr E named : arm_named_<Arm>::values.items) {
                if (value == named) {
                    hit = at;
                    break;
                }
            }
            if (hit != none) break;
        }
    }
    template for (constexpr std::size_t at : arm_positions_<sizeof...(Arms)>.items) {
        if constexpr (!arm_traits_<Arms...[at]>::is_other) {
            if (hit == at) return arms...[at].body();
        }
    }
#pragma GCC diagnostic pop
    constexpr std::size_t other = other_position_<Arms...>();
    if constexpr (other < sizeof...(Arms)) {
        return arms...[other].body(value);
    } else {
        unreachable();
    }
}

// The checks of a match over E, at compile time, with a message that names
// the enumerator of a refusal.  True when the match passes them, so the
// caller instantiates the chain only for a match that passes, and a refusal
// gives one error.
template <class E, class... Arms>
[[nodiscard]] consteval bool check_match_() {
    constexpr Coverage coverage = coverage_<E, Arms...>();
    if constexpr (ClosedEnumeration<E> && coverage.others != 0) {
        static_assert(false, "match_enum: a closed enumeration takes no arm otherwise: name each enumerator in an arm");
        return false;
    } else if constexpr (OpenEnumeration<E> && coverage.others != 1) {
        static_assert(false, "match_enum: an open enumeration takes one arm otherwise, for each value that no "
                             "other arm names");
        return false;
    } else if constexpr (coverage.twice != coverage.count) {
        static_assert(false,
                      message_at_<E>("match_enum: the arms name one value two times, the enumerator ", coverage.twice));
        return false;
    } else if constexpr (ClosedEnumeration<E> && coverage.missing != coverage.count) {
        static_assert(false, message_at_<E>("match_enum: no arm names the enumerator ", coverage.missing));
        return false;
    } else if constexpr (!(std::same_as<typename arm_result_<E, Arms...[0]>::type, typename arm_result_<E, Arms>::type>
                           && ...)) {
        static_assert(false, "match_enum: each arm gives one result type");
        return false;
    } else {
        return true;
    }
}

template <class E, class... Arms>
using match_result_t = typename arm_result_<E, Arms...[0]>::type;

}  // namespace detail

// An arm that names the enumerators and the stated sets of Named, all of one
// enumeration, with its body.
template <auto First, auto... Rest, class Body>
    requires(detail::is_enumerator_pack_<First, Rest...> || detail::is_named_pack_<First, Rest...>)
         && ::foundation::linear::HoldsNoLinearValue<Body>
[[nodiscard]] constexpr EnumArm<std::decay_t<Body>, First, Rest...> arm(Body&& body) noexcept {
    return EnumArm<std::decay_t<Body>, First, Rest...>{std::forward<Body>(body)};
}

// A body that holds a linear value by value binds to this overload, and to
// the same twin of otherwise, so the refusal gives its reason.
template <auto First, auto... Rest, class Body>
    requires(detail::is_enumerator_pack_<First, Rest...> || detail::is_named_pack_<First, Rest...>)
             && (!::foundation::linear::HoldsNoLinearValue<Body>)
void arm(Body&& body) = delete(CRUCIBLE_LINEAR_BODY_REFUSAL);

// The arm of an open enumeration that takes each value that no other arm
// names.
template <class Body>
    requires ::foundation::linear::HoldsNoLinearValue<Body>
[[nodiscard]] constexpr OtherArm<std::decay_t<Body>> otherwise(Body&& body) noexcept {
    return OtherArm<std::decay_t<Body>>{std::forward<Body>(body)};
}

template <class Body>
    requires(!::foundation::linear::HoldsNoLinearValue<Body>)
void otherwise(Body&& body) = delete(CRUCIBLE_LINEAR_BODY_REFUSAL);

// The result of the arm that names value.  The build refuses a set of arms
// that leaves an enumerator of a closed enumeration with no arm, names one
// value two times, or holds an arm otherwise that the enumeration does not
// take.  Linear in the named values at compile time.  At run time, one chain
// of compares and one call of a body.
template <Enumerated E, class... Arms>
    requires(sizeof...(Arms) > 0) && (detail::ArmOf<E, std::remove_cvref_t<Arms>> && ...)
         && (!std::is_void_v<detail::match_result_t<E, std::remove_cvref_t<Arms>...>>)
[[nodiscard]] constexpr detail::match_result_t<E, std::remove_cvref_t<Arms>...>
match_enum(E value, Arms&&... arms) noexcept((detail::is_nothrow_arm_<E, std::remove_cvref_t<Arms>> && ...)) {
    if constexpr (detail::check_match_<E, std::remove_cvref_t<Arms>...>()) {
        return detail::run_arms_<detail::match_result_t<E, std::remove_cvref_t<Arms>...>>(value, arms...);
    } else {
        unreachable();
    }
}

// The same match, for arms that give no result.
template <Enumerated E, class... Arms>
    requires(sizeof...(Arms) > 0) && (detail::ArmOf<E, std::remove_cvref_t<Arms>> && ...)
         && std::is_void_v<detail::match_result_t<E, std::remove_cvref_t<Arms>...>>
constexpr void match_enum(E value,
                          Arms&&... arms) noexcept((detail::is_nothrow_arm_<E, std::remove_cvref_t<Arms>> && ...)) {
    if constexpr (detail::check_match_<E, std::remove_cvref_t<Arms>...>()) {
        detail::run_arms_<void>(value, arms...);
    } else {
        unreachable();
    }
}

}  // namespace foundation::core
