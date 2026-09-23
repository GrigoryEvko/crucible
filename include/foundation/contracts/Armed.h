#pragma once

// A predicate that answers "no" for every type is indistinguishable, at
// every call site, from a predicate nobody armed.  Both let the caller
// through.  These two helpers are the witness that a predicate still has
// arms: the author names, beside the predicate, the types it must accept
// and the types it must refuse, and the build fails on the day an arm is
// deleted rather than on the day someone notices a gate stopped gating.
//
// The shape this exists for was measured, not imagined.  fixy/Qtt.h
// carried two gates whose tables the port left with a false primary and
// no specialization at all, where the old tree had four.  Both gates
// read their table, the table answered "no" to every question, and both
// static assertions became tautologies: Affine<Linear<int>> compiled and
// an exactly-once obligation silently became at-most-once.  Nothing in
// the tree could say so, because a table with no arms and a table whose
// arms all refuse are the same text.
//
// Pred is a class trait, which is the form a template template argument
// can name.  A predicate that exists only as a concept or as a variable
// template gets a one-line trait beside it, and that trait is then what
// these helpers read.
//
// Both directions are needed.  A predicate that answers "yes" for
// everything is as unarmed as one that answers "no", and only one of the
// two is caught by acceptance alone.

#include <cstddef>
#include <meta>
#include <span>
#include <string_view>
#include <type_traits>
#include <vector>

namespace foundation::contracts {

// True when Pred accepts every witness, and there is at least one
// witness.  An empty pack proves nothing and answers false.
template <template <class> class Pred, class... Witnesses>
[[nodiscard]] consteval bool predicate_accepts() noexcept {
    return sizeof...(Witnesses) > 0 && (static_cast<bool>(Pred<Witnesses>::value) && ...);
}

// True when Pred refuses every witness, and there is at least one
// witness.  An empty pack proves nothing and answers false.
template <template <class> class Pred, class... Witnesses>
[[nodiscard]] consteval bool predicate_refuses() noexcept {
    return sizeof...(Witnesses) > 0 && (!static_cast<bool>(Pred<Witnesses>::value) && ...);
}

// ---------------------------------------------------------------------
// The armed cell, and the roster that asks for one.
//
// A static assertion beside a predicate arms it, but no walk can find a
// static assertion, so no walk can say which predicates are armed.  The
// cell is the same two witness lists in a form a walk can find: an
// explicit specialization of armed_cell for the predicate.
//
//   template <>
//   struct foundation::contracts::armed_cell<is_already_linear> {
//       using accepts = witnesses<Linear<int>, Linear<char>>;
//       using refuses = witnesses<int, void*, Affine<int>>;
//   };
//
// The primary is declared and never defined, so a predicate with no cell
// has an incomplete armed_cell, and a walk reads that as unarmed.

template <class... Types>
struct witnesses {};

template <template <class> class Pred>
struct armed_cell;

namespace detail {

template <template <class> class Pred, class Witnesses>
struct accepts_every;
template <template <class> class Pred, class... Types>
struct accepts_every<Pred, witnesses<Types...>> : std::bool_constant<predicate_accepts<Pred, Types...>()> {};

template <template <class> class Pred, class Witnesses>
struct refuses_every;
template <template <class> class Pred, class... Types>
struct refuses_every<Pred, witnesses<Types...>> : std::bool_constant<predicate_refuses<Pred, Types...>()> {};

}  // namespace detail

// True when the cell for Pred holds in both directions.  Each list must
// be non-empty, for the reason predicate_accepts states.
template <template <class> class Pred>
inline constexpr bool armed_cell_holds_v =
    detail::accepts_every<Pred, typename armed_cell<Pred>::accepts>::value
    && detail::refuses_every<Pred, typename armed_cell<Pred>::refuses>::value;

// The predicate roster.
//
// A predicate here is a class template whose name asks a question, under
// the rule in the code guide that the name of each predicate contains a
// question verb: is_, has_, can_,
// should_, must_, will_, was_, needs_, or the verbs admits and conveys
// inside the name.  The walk reads the name, because the only other
// discriminator is to instantiate each template, and an instantiation
// can stop the build with a static assertion that belongs to the
// template.  A predicate whose name asks no question is not walked, and
// that is a naming defect the guide already rejects.
//
// A walked template that cannot take one type argument cannot hold a
// cell, and the walk counts it unarmed rather than skipping it.  An
// unhandled shape is therefore unproven, never passed.
//
// Namespaces whose name contains `self_test` hold the scaffolding of a
// header's own checks, not gates, and the walk skips them.

[[nodiscard]] consteval bool names_a_predicate(std::string_view name) noexcept {
    constexpr std::string_view leading[] = {"is_", "has_", "can_", "should_", "must_", "will_", "was_", "needs_",
                                            "admits_", "conveys_"};
    constexpr std::string_view inner[] = {"_is_", "_has_", "_can_", "_needs_", "_admits", "_conveys"};
    for (const std::string_view prefix : leading) {
        if (name.starts_with(prefix)) return true;
    }
    for (const std::string_view infix : inner) {
        if (name.find(infix) != std::string_view::npos) return true;
    }
    return false;
}

namespace detail {

consteval void collect_predicates(std::meta::info scope, std::vector<std::meta::info>& found) {
    for (const std::meta::info member : std::meta::members_of(scope, std::meta::access_context::unchecked())) {
        if (std::meta::is_namespace(member) && !std::meta::is_namespace_alias(member)) {
            if (std::meta::has_identifier(member)
                && std::meta::identifier_of(member).find("self_test") != std::string_view::npos) {
                continue;
            }
            collect_predicates(member, found);
        } else if (std::meta::is_class_template(member) && std::meta::has_identifier(member)
                   && names_a_predicate(std::meta::identifier_of(member))) {
            found.push_back(member);
        }
    }
}

}  // namespace detail

// Every predicate the walk finds under the given namespaces.
// Complexity: linear in the number of declarations under them.
[[nodiscard]] consteval std::vector<std::meta::info> predicate_roster(std::span<const std::meta::info> scopes) {
    std::vector<std::meta::info> found;
    for (const std::meta::info scope : scopes) detail::collect_predicates(scope, found);
    return found;
}

// True when the predicate that `tmpl` reflects has a cell, and the cell
// holds.  A cell that does not hold is not an arm: it is the unarmed
// shape with a comment beside it.
[[nodiscard]] consteval bool is_armed(std::meta::info tmpl) {
    if (!std::meta::can_substitute(^^armed_cell, {tmpl})) return false;
    const std::meta::info cell = std::meta::substitute(^^armed_cell, {tmpl});
    if (!std::meta::is_complete_type(cell)) return false;
    return std::meta::extract<bool>(std::meta::substitute(^^armed_cell_holds_v, {tmpl}));
}

// The walk's verdict over a roster and a ledger of predicates known to
// be unarmed.  The ledger is how the debt stays visible without letting
// it grow: a predicate outside it must be armed, and an entry inside it
// must be a walked predicate that is still unarmed.  An entry that was
// armed, or that names no walked predicate, is stale, and a stale entry
// fails the walk the same way a new unarmed predicate does.
struct ArmedRosterVerdict {
    std::size_t walked = 0;
    std::size_t armed = 0;
    std::size_t ledgered = 0;
    std::size_t unarmed_outside_ledger = 0;
    std::size_t stale_ledger_entries = 0;
};

[[nodiscard]] consteval ArmedRosterVerdict armed_roster_verdict(std::span<const std::meta::info> scopes,
                                                                std::span<const std::meta::info> ledger) {
    ArmedRosterVerdict verdict;
    const std::vector<std::meta::info> roster = predicate_roster(scopes);
    verdict.walked = roster.size();
    for (const std::meta::info predicate : roster) {
        bool is_ledgered = false;
        for (const std::meta::info entry : ledger) {
            if (entry == predicate) is_ledgered = true;
        }
        if (is_armed(predicate)) {
            ++verdict.armed;
            if (is_ledgered) ++verdict.stale_ledger_entries;
        } else if (is_ledgered) {
            ++verdict.ledgered;
        } else {
            ++verdict.unarmed_outside_ledger;
        }
    }
    for (const std::meta::info entry : ledger) {
        bool is_walked = false;
        for (const std::meta::info predicate : roster) {
            if (entry == predicate) is_walked = true;
        }
        if (!is_walked) ++verdict.stale_ledger_entries;
    }
    return verdict;
}

// The first predicate the walk finds unarmed outside the ledger, or the
// null reflection.  A static assertion names it in its text.
[[nodiscard]] consteval std::meta::info first_unarmed_outside_ledger(std::span<const std::meta::info> scopes,
                                                                     std::span<const std::meta::info> ledger) {
    for (const std::meta::info predicate : predicate_roster(scopes)) {
        bool is_ledgered = false;
        for (const std::meta::info entry : ledger) {
            if (entry == predicate) is_ledgered = true;
        }
        if (!is_ledgered && !is_armed(predicate)) return predicate;
    }
    return {};
}

namespace detail::armed_self_test {

template <class T>
struct always_true : std::true_type {};

template <class T>
struct always_false : std::false_type {};

template <class T>
struct is_int : std::bool_constant<std::is_same_v<T, int>> {};

static_assert(predicate_accepts<is_int, int>());
static_assert(predicate_refuses<is_int, char, void>());
static_assert(!predicate_accepts<is_int, char>());
static_assert(!predicate_refuses<is_int, int>());

// An empty witness pack proves nothing, so neither helper answers yes.
static_assert(!predicate_accepts<always_true>());
static_assert(!predicate_refuses<always_false>());

// The two unarmed shapes, each caught by exactly one of the pair.
static_assert(!predicate_accepts<always_false, int>());
static_assert(!predicate_refuses<always_true, int>());

}  // namespace detail::armed_self_test

// The roster walk, over a stand-in namespace.  Four predicates: one with
// a cell that holds, one with no cell, one with a cell whose accepting
// witness the predicate refuses, and one whose parameter is a value.
// The walk must count one arm and three unarmed predicates.
//
// The walk skips a self_test namespace only below its root.  So a roster
// over the whole foundation tree skips this stand-in, and the walk
// rooted here reads it.
namespace detail::armed_roster_self_test_stand_in {

template <class T>
struct is_char : std::bool_constant<std::is_same_v<T, char>> {};

template <class T>
struct is_long : std::bool_constant<std::is_same_v<T, long>> {};

template <class T>
struct is_short : std::bool_constant<std::is_same_v<T, short>> {};

// Not a predicate by its name, so the walk does not read it.
template <class T>
struct width_of : std::integral_constant<std::size_t, sizeof(T)> {};

// A predicate by its name whose parameter is a value.  It cannot hold a
// cell, so the walk counts it unarmed.
template <int N>
struct is_even : std::bool_constant<N % 2 == 0> {};

}  // namespace detail::armed_roster_self_test_stand_in

template <>
struct armed_cell<detail::armed_roster_self_test_stand_in::is_char> {
    using accepts = witnesses<char>;
    using refuses = witnesses<int, signed char>;
};

template <>
struct armed_cell<detail::armed_roster_self_test_stand_in::is_short> {
    using accepts = witnesses<int>;
    using refuses = witnesses<short>;
};

namespace detail::armed_roster_self_test {

inline constexpr std::meta::info stand_in_scope[] = {^^::foundation::contracts::detail::armed_roster_self_test_stand_in};
inline constexpr std::meta::info no_ledger[] = {^^::foundation::contracts::detail::armed_roster_self_test_stand_in::is_long};

static_assert(names_a_predicate("is_char") && names_a_predicate("row_admits_bg_") && !names_a_predicate("width_of"));
static_assert(armed_cell_holds_v<::foundation::contracts::detail::armed_roster_self_test_stand_in::is_char>);
static_assert(!armed_cell_holds_v<::foundation::contracts::detail::armed_roster_self_test_stand_in::is_short>);

static_assert(armed_roster_verdict(stand_in_scope, {}).walked == 4);
static_assert(armed_roster_verdict(stand_in_scope, {}).armed == 1);
static_assert(armed_roster_verdict(stand_in_scope, {}).unarmed_outside_ledger == 3,
              "the walk must count the predicate with no cell, the predicate whose cell does not hold, and the "
              "predicate that cannot hold a cell.");
static_assert(armed_roster_verdict(stand_in_scope, no_ledger).unarmed_outside_ledger == 2);
static_assert(armed_roster_verdict(stand_in_scope, no_ledger).ledgered == 1);
static_assert(armed_roster_verdict(stand_in_scope, no_ledger).stale_ledger_entries == 0);

// A ledger entry that is armed is stale, and so is one the walk never
// reaches.
inline constexpr std::meta::info armed_in_ledger[] = {
    ^^::foundation::contracts::detail::armed_roster_self_test_stand_in::is_char};
inline constexpr std::meta::info unwalked_in_ledger[] = {
    ^^::foundation::contracts::detail::armed_roster_self_test_stand_in::width_of};
static_assert(armed_roster_verdict(stand_in_scope, armed_in_ledger).stale_ledger_entries == 1);
static_assert(armed_roster_verdict(stand_in_scope, unwalked_in_ledger).stale_ledger_entries == 1);

static_assert(first_unarmed_outside_ledger(stand_in_scope, no_ledger)
                  == ^^::foundation::contracts::detail::armed_roster_self_test_stand_in::is_short
              || first_unarmed_outside_ledger(stand_in_scope, no_ledger)
                     == ^^::foundation::contracts::detail::armed_roster_self_test_stand_in::is_even);

}  // namespace detail::armed_self_test

}  // namespace foundation::contracts
