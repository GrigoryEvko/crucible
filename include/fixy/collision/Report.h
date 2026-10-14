#pragma once

// The checks that compare the parts of the collision rules with each
// other.
//
// fixy/Collision.h holds the rules and the joined atom roster, and
// fixy/collision/Codes.h holds the codes and the corpus.  Each check of
// this header compares two lists that a person writes separately.  A
// check gives the names of the offenders and not a bool, so its message
// can name the roster, the code or the member to correct.  The check
// files call these checks, and no other header includes this header.

#include <fixy/Atom.h>
#include <fixy/Axis.h>
#include <fixy/Collision.h>
#include <fixy/collision/Codes.h>
#include <foundation/reflect/Anchor.h>

#include <cstddef>
#include <meta>

namespace fixy {

namespace collision {

// ---------------------------------------------------------------------
// Every declared atom roster is joined into all_atom_roster.
//
// The join in fixy/Collision.h is a hand-written list, and the guard
// beside it only notices a missing family when the family's AXIS ends up
// with no atoms at all.  A roster whose axis is populated by some other family is
// invisible to it: the atoms exist, nothing reads them, and the guard
// whose name promises completeness reports clean.  That is how
// fixy/concurrent/Spawn.h's three atoms sat outside the population while the
// check said every axis was covered, and it is the same shape as an
// allowlist key naming a file that is gone — a guard that cannot fail
// the way its name implies.
//
// So the relation is stated over the DECLARATIONS rather than over the
// axes.  A `*_atom_roster` alias in fixy::atom::detail must have every
// one of its atoms reachable from all_atom_roster.  A roster composed of
// sub-rosters satisfies it through the parent, which is why the io, fs,
// mmap and leak sets pass without being named in the join.
//
// The walk sees what its translation unit has included, so the check
// file of this header covers the families that this header includes and
// no more.  The complete answer needs a vantage point after every header
// that can declare a roster: the walk units of
// test/layer/walks_across_headers.cpp compile the check file of this
// header after each such header (test/layer/walk-checks.txt).  The
// fixtures neg_atom_roster_declared_not_joined and
// neg_atom_roster_sample_set_joined show that each check fires.
// Both this file and the check that every atom has a consumer read this
// derivation rather than building a second set.
//
// The Site parameter is what makes that second vantage point possible.
// std::meta::members_of answers as of the point where the walk is
// INSTANTIATED, and a specialization is instantiated once: a later call
// to the same specialization returns the earlier point's answer.  A
// non-template walk is frozen at this header's own line and can never
// see a roster declared by a header included after it.  Each vantage
// point passes its own tag type instead, so its call is a fresh
// specialization that walks the namespace as its translation unit has it
// at that line.  Reusing another site's tag silently reuses that site's
// answer, which is the one way to get a wrong clean result here.

namespace detail {

// The members of a roster, each dealiased, so two spellings of one atom
// give one reflection.
template <class Roster>
[[nodiscard]] consteval std::vector<std::meta::info> dealiased_roster_members_() {
    std::vector<std::meta::info> types = std::meta::template_arguments_of(std::meta::dealias(^^Roster));
    std::meta::info* const type = types.data();
    for (std::size_t place = 0; place < types.size(); ++place)
        type[place] = std::meta::dealias(type[place]);
    return types;
}

template <class Roster>
inline constexpr auto dealiased_roster_members_v = std::define_static_array(dealiased_roster_members_<Roster>());

// True when each member of the roster is in the population, the joined
// population unless a caller names another.  The loops compare
// reflections, so the walk instantiates one function for each roster and
// none for each atom.  The population is a template parameter, so only a
// call reads its table.  Complexity: the roster size times the
// population, in plain compares of reflections.
template <class Roster, class Population = all_atom_roster>
[[nodiscard]] consteval bool every_atom_joined_() noexcept {
    for (const std::meta::info member : dealiased_roster_members_v<Roster>) {
        bool is_held = false;
        for (const std::meta::info joined : dealiased_roster_members_v<Population>) {
            if (joined == member) {
                is_held = true;
                break;
            }
        }
        if (!is_held) return false;
    }
    return true;
}

}  // namespace detail

// The offending rosters' names, comma-separated, or empty when every
// declared roster is joined.  Answering with the NAMES rather than a bool
// is what lets the diagnostic say which roster and leaves the reader one
// edit from the join site.
//
// Every offender, not the first.  Reporting only the first would make one
// known offender mask every other, which is how a second orphan would
// arrive unnoticed while the guard was already red for the first — and it
// would also make the guard's own self-test unable to tell its injected
// probe from the offender already there.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"

template <class Site>
[[nodiscard]] consteval std::string_view roster_declared_but_not_joined() {
    ::foundation::reflect::anchored_t<^^Site, std::string> offenders;
    static constexpr auto members =
        std::define_static_array(static_cast<::foundation::reflect::anchored_t<^^Site, std::vector<std::meta::info>>>(
            std::meta::members_of(^^::fixy::atom::detail, std::meta::access_context::current())));
    template for (constexpr auto member : members) {
        if constexpr (std::meta::is_type_alias(member)) {
            constexpr std::string_view name = std::meta::identifier_of(member);
            if constexpr (name.ends_with("_atom_roster")) {
                using Roster = [:std::meta::dealias(member):];
                if constexpr (!detail::every_atom_joined_<Roster>()) {
                    if (!offenders.empty()) offenders += ", ";
                    offenders += name;
                }
            }
        }
    }
    if (offenders.empty()) return {};
    return std::string_view{std::define_static_string(offenders)};
}

// The other direction, and it is what keeps the first from being dodged
// by a rename.  A set named `*_atom_samples` is representative
// instantiations for a local self-test and is deliberately OUTSIDE the
// population; if one is joined, the two categories have blurred and the
// name no longer says which it is.
template <class Site>
[[nodiscard]] consteval std::string_view sample_set_wrongly_joined() {
    ::foundation::reflect::anchored_t<^^Site, std::string> offenders;
    static constexpr auto members =
        std::define_static_array(static_cast<::foundation::reflect::anchored_t<^^Site, std::vector<std::meta::info>>>(
            std::meta::members_of(^^::fixy::atom::detail, std::meta::access_context::current())));
    template for (constexpr auto member : members) {
        if constexpr (std::meta::is_type_alias(member)) {
            constexpr std::string_view name = std::meta::identifier_of(member);
            if constexpr (name.ends_with("_atom_samples")) {
                using Samples = [:std::meta::dealias(member):];
                if constexpr (detail::every_atom_joined_<Samples>()) {
                    if (!offenders.empty()) offenders += ", ";
                    offenders += name;
                }
            }
        }
    }
    if (offenders.empty()) return {};
    return std::string_view{std::define_static_string(offenders)};
}

#pragma GCC diagnostic pop

// The two diagnostics.
//
// P2741R3 lets a static_assert message be computed, and that is the only
// reason this relation can NAME the offending roster instead of saying
// that one of them is wrong.  A reader gets the roster and the single
// line to edit, which is the difference between a guard that reports a
// fault and a guard that reports a fault somebody can fix.
template <class Site>
[[nodiscard]] consteval std::string_view roster_join_diagnostic() {
    const std::string_view offenders = roster_declared_but_not_joined<Site>();
    if (offenders.empty()) return {};
    ::foundation::reflect::anchored_t<^^Site, std::string> message =
        "fixy/collision/Report.h: the atom-population relation: declared in fixy::atom::detail and NOT joined into "
        "fixy::collision::all_atom_roster, so their atoms sit outside the population that every axis-coverage "
        "and collision-rule check reads: ";
    // One character at a time.  The offenders are in a static string, and an
    // append of its pointer compares that pointer with null.  Under
    // -fno-delete-null-pointer-checks, GCC cannot fold that compare in a
    // constant evaluation, and the check then gives no message.
    for (const char letter : offenders)
        message += letter;
    message += ".  Join each at the all_atom_roster alias in include/fixy/Collision.h.  If one is representative "
               "instantiations for a local self-test rather than a family population, rename it to end in "
               "_atom_samples instead — that spelling says so, and sample_set_wrongly_joined() holds it to it.";
    return std::string_view{std::define_static_string(message)};
}

template <class Site>
[[nodiscard]] consteval std::string_view sample_set_diagnostic() {
    const std::string_view offenders = sample_set_wrongly_joined<Site>();
    if (offenders.empty()) return {};
    ::foundation::reflect::anchored_t<^^Site, std::string> message =
        "fixy/collision/Report.h: the atom-population relation: these sample sets ARE joined into "
        "fixy::collision::all_atom_roster: ";
    // One character at a time, for the reason in roster_join_diagnostic().
    for (const char letter : offenders)
        message += letter;
    message += ".  A sample set is a set of INSTANTIATIONS of a parametric family, kept outside the population "
               "because the family has no finite membership and no list can enumerate it; the instantiations are "
               "placeholders chosen to instantiate a local self-test, not grades anybody writes.  A family whose "
               "members are distinct grades is finite and belongs in the joined population instead, under a name "
               "ending in _atom_roster.  Joining a sample set blurs the two categories, and a blurred boundary is "
               "what would let a real family be renamed out of roster_declared_but_not_joined() rather than joined "
               "into the population.  So: drop each from the all_atom_roster alias in include/fixy/Collision.h, or, "
               "if its members really are distinct grades, rename it to end in _atom_roster and leave it joined.";
    return std::string_view{std::define_static_string(message)};
}

// ---------------------------------------------------------------------
// The pin that reads the implementation rather than the specification.
//
// live_rules names one `<code>_ok` member per rule it implements.  Two
// checks in the check file of this header walk those members: the first
// asks that every code the corpus calls Live has one, the second that no
// `_ok` member exists without a Live entry.  Together they bind the corpus to the code that
// actually runs, which is the step that `live == roster - pending`
// arithmetic skips.

namespace detail {

[[nodiscard]] consteval bool member_is_the_gate_for_(std::meta::info member, std::string_view code) noexcept {
    if (!std::meta::has_identifier(member)) return false;
    const std::string_view id = std::meta::identifier_of(member);
    return id.size() == code.size() + 3 && id.starts_with(code) && id.ends_with("_ok");
}

template <class Anchor = void>
[[nodiscard]] consteval std::size_t implemented_rule_count_() noexcept {
    std::size_t found = 0;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto member : std::define_static_array(
                      static_cast<::foundation::reflect::anchored_t<^^Anchor, std::vector<std::meta::info>>>(
                          std::meta::members_of(^^rules_of<void>, std::meta::access_context::unchecked())))) {
        if constexpr (std::meta::has_identifier(member)) {
            if constexpr (std::meta::identifier_of(member).ends_with("_ok")) {
                ++found;
            }
        }
    }
#pragma GCC diagnostic pop
    return found;
}

template <class Anchor = void>
[[nodiscard]] consteval bool every_live_entry_is_implemented_() noexcept {
    for (const corpus_entry& entry : rule_corpus) {
        if (entry.disposition != Disposition::Live) continue;
        bool implemented = false;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
        template for (constexpr auto member : std::define_static_array(
                          static_cast<::foundation::reflect::anchored_t<^^Anchor, std::vector<std::meta::info>>>(
                              std::meta::members_of(^^rules_of<void>, std::meta::access_context::unchecked())))) {
            if (member_is_the_gate_for_(member, entry.code)) implemented = true;
        }
#pragma GCC diagnostic pop
        if (!implemented) return false;
    }
    return true;
}

}  // namespace detail

// The verdict rows are the third hand-written list of the same set, and
// two pins in the check file of this header bind it to the other two.
// The count is against the member walk rather than against
// live_rule_count so that a rule written with an `_ok` member and no
// verdict row is named even before it reaches the corpus.
namespace detail {

template <class Anchor = void>
[[nodiscard]] consteval bool every_ok_member_has_a_verdict_row_() noexcept {
    bool all_rowed = true;
    // The list of codes is read one time, before the walk.  A call inside
    // the expansion statement builds the list again for each member.  The
    // payload depends on Anchor, so only a call instantiates the class.
    constexpr std::string_view every_live_code =
        rules_of<::foundation::reflect::anchored_t<^^Anchor, void>>::every_code();
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto member : std::define_static_array(
                      static_cast<::foundation::reflect::anchored_t<^^Anchor, std::vector<std::meta::info>>>(
                          std::meta::members_of(^^rules_of<void>, std::meta::access_context::unchecked())))) {
        if constexpr (std::meta::has_identifier(member)) {
            constexpr std::string_view id = std::meta::identifier_of(member);
            if constexpr (id.size() > 3 && id.ends_with("_ok")) {
                // The trailing space is what keeps a code from matching
                // inside a longer one.
                std::string wanted{id.substr(0, id.size() - 3)};
                wanted += ' ';
                if (!::fixy::detail::text_contains(every_live_code, wanted)) all_rowed = false;
            }
        }
    }
#pragma GCC diagnostic pop
    return all_rowed;
}

}  // namespace detail

}  // namespace collision

}  // namespace fixy
