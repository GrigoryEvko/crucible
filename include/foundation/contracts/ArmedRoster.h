#pragma once

// The predicate roster: the walk that finds each predicate under a set of
// namespaces and asks whether its cell from foundation/contracts/Armed.h
// holds.
//
// The walk is in its own header.  Its functions are not templates, so no
// translation unit can specialize an answer.  But GCC instantiates each
// template that the body of such a function calls in each includer, and
// most includers of Armed.h read only a cell.
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
// cell.  It can hold an instance cell.  Without one, the walk counts it
// unarmed rather than skipping it.  An unhandled shape is therefore
// unproven, never passed.
//
// Namespaces whose name contains `self_test` hold the scaffolding of a
// header's own checks, not gates, and the walk skips them.

#include <foundation/contracts/Armed.h>

#include <cstddef>
#include <meta>
#include <span>
#include <string_view>
#include <vector>

namespace foundation::contracts {

[[nodiscard]] consteval bool names_a_predicate(std::string_view name) noexcept {
    constexpr std::string_view leading[] = {"is_",   "has_", "can_",   "should_", "must_",
                                            "will_", "was_", "needs_", "admits_", "conveys_"};
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
    for (const std::meta::info scope : scopes)
        detail::collect_predicates(scope, found);
    return found;
}

// True when the predicate that `tmpl` reflects has a cell or an instance
// cell, and that cell holds.  A cell that does not hold is not an arm: it
// is the unarmed shape with a comment beside it.  A cell is read first.
// The instance cell is read only when the predicate has no cell.
[[nodiscard]] consteval bool is_armed(std::meta::info tmpl) {
    if (std::meta::can_substitute(^^armed_cell, {tmpl})) {
        const std::meta::info cell = std::meta::substitute(^^armed_cell, {tmpl});
        if (std::meta::is_complete_type(cell)) {
            return std::meta::extract<bool>(std::meta::substitute(^^armed_cell_holds_v, {tmpl}));
        }
    }
    const std::meta::info key = std::meta::reflect_constant(tmpl);
    const std::meta::info instance_cell = std::meta::substitute(^^armed_instances, {key});
    if (!std::meta::is_complete_type(instance_cell)) return false;
    return std::meta::extract<bool>(std::meta::substitute(^^armed_instances_hold_v, {key}));
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

}  // namespace foundation::contracts
