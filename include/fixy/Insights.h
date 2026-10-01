#pragma once

// What a refusal says beyond its name: why the rule exists, how the
// violation reaches a call site, and the compliant line beside the
// violating one.  foundation/diag/Insights.h reads those four fields
// and a severity off an insight_provider, and this header supplies
// them for the two families fixy's gate refuses with: the per-axis
// duplicate tag of fixy/Reject.h and the corpus entries of
// fixy/Corpus.h.
//
// The per-axis text is generated, not written.  Prose that names the
// strict pole of an axis by hand can contradict the table, and the
// compiler does not read prose.  So one partial specialisation serves
// every axis and splices the axis name and the strict pole's name out
// of Axis.h by reflection.  The text then cannot say a pole that the
// table does not.  The check file of this header walks the enum and asks
// each axis for its provider, so an axis added to the enum is covered
// the day it is declared.
//
// The six corpus entries have hand-written text.  The atom spellings in
// the examples are the ones the tree ships, and the pins in the check
// file of this header hold the policies that the examples cite.

#include <fixy/Axis.h>
#include <fixy/Corpus.h>
#include <fixy/Reject.h>
#include <fixy/Tags.h>
#include <foundation/diag/Insights.h>
#include <foundation/reflect/EnumName.h>

#include <cstddef>
#include <meta>
#include <string>
#include <string_view>
#include <type_traits>

namespace fixy::insights::detail {

[[nodiscard]] consteval std::string_view digits_(unsigned long long value) noexcept {
    std::string text;
    do {
        text.insert(text.begin(), static_cast<char>('0' + static_cast<int>(value % 10)));
        value /= 10;
    } while (value != 0);
    return std::define_static_string(text);
}

// The name of an axis's strict pole, read off the table.  A value pole
// is named by its value: the enumerator's identifier, or the digits of
// an integer.  A type pole is named by its class, or by its template
// when it is a specialisation, so pole::Unconstrained<A> reads as
// Unconstrained and Row<> as Row.  The Type axis has no pole; its grade
// is the payload the caller named.
template <Axis A>
[[nodiscard]] consteval std::string_view strict_pole_name_() noexcept {
    if constexpr (::fixy::IsCallerSupplied<A>) {
        return "the payload type the binding names";
    } else {
        using Strict = typename ::fixy::axis_traits<A>::strict;
        if constexpr (requires { Strict::value; }) {
            constexpr auto value = Strict::value;
            if constexpr (std::is_enum_v<decltype(value)>) {
                return ::foundation::reflect::enum_name(value);
            } else {
                return digits_(static_cast<unsigned long long>(value));
            }
        } else {
            constexpr std::meta::info pole = std::meta::dealias(^^Strict);
            if constexpr (std::meta::has_template_arguments(pole)) {
                return std::meta::identifier_of(std::meta::template_of(pole));
            } else {
                return std::meta::identifier_of(pole);
            }
        }
    }
}

template <Axis A>
[[nodiscard]] consteval std::string_view why_() noexcept {
    std::string text{"An axis carries one grade.  A binding that says nothing about the "};
    text += ::fixy::axis_name(A);
    text += " axis takes its strict pole, ";
    text += strict_pole_name_<A>();
    text += ", and one atom on the axis relaxes it to exactly that atom's grade.  Two atoms on the axis do not say "
            "which grade the binding means, and the resolver cannot pick one without granting a claim nobody made.";
    return std::define_static_string(text);
}

template <Axis A>
[[nodiscard]] consteval std::string_view symptom_() noexcept {
    std::string text{"Two atoms from the "};
    text += ::fixy::axis_name(A);
    text += " family in one fixy::fn pack, usually after two packs were merged, or after a role that already "
            "names the axis was extended with a second atom on it.";
    return std::define_static_string(text);
}

template <Axis A>
[[nodiscard]] consteval std::string_view correct_() noexcept {
    std::string text{"fixy::fn<T, atom_on_"};
    text += ::fixy::axis_name(A);
    text += ">  // or fixy::fn<T>: the strict pole ";
    text += strict_pole_name_<A>();
    text += " needs no atom";
    return std::define_static_string(text);
}

template <Axis A>
[[nodiscard]] consteval std::string_view violating_() noexcept {
    std::string text{"fixy::fn<T, atom_on_"};
    text += ::fixy::axis_name(A);
    text += ", second_atom_on_";
    text += ::fixy::axis_name(A);
    text += ">  // two grades on ";
    text += ::fixy::axis_name(A);
    return std::define_static_string(text);
}

}  // namespace fixy::insights::detail

namespace foundation::diag {

// One partial specialisation for every axis.  Each field is computed
// from the axis, so the 33 providers exist the moment the enum does
// and say nothing the table does not.
template <::fixy::Axis A>
struct insight_provider<::fixy::duplicate_atom_on<A>> {
    static constexpr Severity severity = Severity::Error;
    static constexpr std::string_view why_this_matters = ::fixy::insights::detail::why_<A>();
    static constexpr std::string_view symptom_pattern = ::fixy::insights::detail::symptom_<A>();
    static constexpr std::string_view correct_example = ::fixy::insights::detail::correct_<A>();
    static constexpr std::string_view violating_example = ::fixy::insights::detail::violating_<A>();
};

}  // namespace foundation::diag

// ---------------------------------------------------------------------
// The six corpus entries, by hand.  Entries whose why_this_matters
// reads cite() take the citation from the tag itself, so the two
// surfaces cannot drift apart.

CRUCIBLE_DIAG_INSIGHTS_QV(::fixy::corpus::classified_io_without_declassify, ::foundation::diag::Severity::Fatal,
                          "Sabelfeld-Myers 2003 (after Volpano-Smith-Irvine 1996 type-system foundation) "
                          "implicit information flow: a classified value reaches an I/O boundary without an "
                          "audit-discharging declassification policy.  Sequential IFC type systems require an "
                          "explicit policy at every classified-to-IO transition, and only a policy whose mask "
                          "names IO discharges it: AuditedLogging, WireSerialize or UserDisplay.",
                          "The Security grade is the strict pole, atom::as_secret, atom::as_classified, or an "
                          "atom::declassify<Policy> whose policy does not license IO, and the Effect grade is "
                          "an atom::with<...> row naming IO.",
                          "fixy::fn<T, atom::with_io, atom::as_public>  // role::IoFunction; or role::PublicEmit<T, "
                          "tags::secret_policy::WireSerialize> for a licensed export",
                          "fixy::fn<T, atom::as_secret, atom::with_io>  // classified IO");

CRUCIBLE_DIAG_INSIGHTS_QV(::fixy::corpus::classified_bg_without_declassify, ::foundation::diag::Severity::Fatal,
                          "Smith-Volpano 1998 + Sabelfeld-Sands 2000 + Hedin-Sabelfeld 2012 concurrent "
                          "information flow: a classified value crosses into a background-thread context whose "
                          "scheduling becomes secret-dependent.  Sequential IFC is UNSOUND under concurrency, "
                          "and no policy discharges the Bg channel.",
                          "The Security grade is the strict pole, atom::as_secret, atom::as_classified, or an "
                          "atom::declassify<Policy>, and the Effect grade is an atom::with<...> row naming Bg.",
                          "fixy::fn<T, atom::with<Effect::Bg, Effect::Alloc>, atom::as_public>  // role::BgWorker",
                          "fixy::fn<T, atom::as_secret, atom::with_bg>  // classified Bg");

CRUCIBLE_DIAG_INSIGHTS_QV(::fixy::corpus::staleness_secret_without_declassify, ::foundation::diag::Severity::Fatal,
                          ::fixy::corpus::staleness_secret_without_declassify::cite(),
                          "The Security grade is a carrier or an atom::declassify<Policy> whose policy does "
                          "not discharge Staleness, and the Staleness grade is atom::stale_to<TauMax>.",
                          "fixy::fn<T, atom::stale_to<100>, atom::declassify<tags::secret_policy::AuthorizedReplay>>",
                          "fixy::fn<T, atom::as_secret, atom::stale_to<100>>  // no replay discharge");

CRUCIBLE_DIAG_INSIGHTS_QV(::fixy::corpus::ghost_runtime_observable, ::foundation::diag::Severity::Fatal,
                          ::fixy::corpus::ghost_runtime_observable::cite(),
                          "The Usage grade is atom::ghost and the Effect grade is an atom::with<...> row naming "
                          "Alloc, IO, Block or Bg.",
                          "fixy::fn<T, atom::with_io, atom::as_public>  // drop ghost: linear is the strict pole",
                          "fixy::fn<T, atom::ghost, atom::with_io>  // erased code that must emit");

CRUCIBLE_DIAG_INSIGHTS_QV(::fixy::corpus::internal_io_without_declassify, ::foundation::diag::Severity::Fatal,
                          "Bell-LaPadula 1973 + Volpano-Smith-Irvine 1996 + Sabelfeld-Myers 2003 "
                          "no-write-down: org-internal data (atom::as_internal, below the strict pole but "
                          "above public) flows into an I/O sink without an audit-discharging declassification "
                          "policy.  Every non-public to public crossing requires a discharge, not just the "
                          "classified and secret tiers.  An internal value leaves through a policy whose mask "
                          "names IO, written in place of atom::as_internal.",
                          "The Security grade is atom::as_internal and the Effect grade is an atom::with<...> "
                          "row naming IO.",
                          "fixy::fn<T, atom::with_io, atom::as_public>  // or drop the IO atom",
                          "fixy::fn<T, atom::as_internal, atom::with_io>  // internal write-down");

CRUCIBLE_DIAG_INSIGHTS_QV(::fixy::corpus::internal_bg_without_declassify, ::foundation::diag::Severity::Fatal,
                          ::fixy::corpus::internal_bg_without_declassify::cite(),
                          "The Security grade is atom::as_internal and the Effect grade is an atom::with<...> "
                          "row naming Bg.",
                          "fixy::fn<T, atom::with_bg, atom::as_public>  // or run on the foreground thread",
                          "fixy::fn<T, atom::as_internal, atom::with_bg>  // internal concurrent write-down");
