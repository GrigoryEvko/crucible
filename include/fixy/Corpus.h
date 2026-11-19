#pragma once

// The refused-combination corpus: bindings the information-flow and
// ghost-code literature shows unsound, refused at fn's tier 5 beside
// the collision rules.
//
// A collision rule is a theorem about two axes.  A corpus entry is a
// pattern over several: a Security grade, an effect row, and the
// presence or absence of a declassification that discharges the
// channel the row opens.  Each entry names the paper the pattern comes
// from, because the citation is the reason the binding is refused and
// the message a reader gets.
//
// The corpus holds no entry that needs a discharge the tree cannot
// give: a mask for termination or for catastrophic replay, which no
// policy has, or a Secret<T> payload read off the type.
//
// What a declassification discharges
// ----------------------------------
// A policy licenses exactly the channels its mask names, and a binding
// that names declassify<Policy> stays classified on each other channel.
// fixy/Atom.h holds the mask of each policy, discharge_mask_of, beside the
// Security atoms, so the collision rules read the same masks.  No policy
// licenses Bg.
//
// Over the premises, not over the pack
// ------------------------------------
// Each matcher reads the premises of the binding, from the sealed verdict
// cell of fixy/Collision.h, computed from the payload and the pack alone so
// that no entry completes fn and recurses through the gate.  An entry that
// reads IO, Bg or an observable effect reads the row of the binding, which
// joins the Effect grade with the lifts of the atoms.  Each matcher is a
// static member function that is not a template, so no unit can give an
// entry another answer.
//
// One axis carries one grade, so "classified on a channel" is one
// question about the Security grade and the channel: the grade is a
// classified carrier, or it is a declassification whose mask does not
// name the channel.

#include <fixy/Atom.h>
#include <fixy/Axis.h>
#include <fixy/Collision.h>
#include <fixy/Predicates.h>
#include <fixy/Tags.h>
#include <foundation/Platform.h>
#include <foundation/diag/Tag.h>
#include <foundation/effects/Effect.h>
#include <foundation/effects/Row.h>
#include <foundation/reflect/Anchor.h>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <meta>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace fixy::corpus {

namespace detail {

// A policy that licenses a channel is a decision a reviewer must see.
// This walks the policy namespace of fixy/Tags.h rather than a hand
// list, so the count reads every declared policy; fixy/Secret.h proves
// the declared set and the admitted set are one set.  The walk is a
// template, so that only a translation unit that calls it expands it.
// Its list depends on Anchor (foundation/reflect/Anchor.h).
template <class Anchor = void>
[[nodiscard]] consteval std::size_t non_none_policy_count_() noexcept {
    std::size_t count = 0;
    static constexpr auto members =
        std::define_static_array(static_cast<::foundation::reflect::anchored_t<^^Anchor, std::vector<std::meta::info>>>(
            std::meta::members_of(^^::fixy::tags::secret_policy, std::meta::access_context::unchecked())));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto member : members) {
        if constexpr (std::meta::is_type(member) && !std::meta::is_type_alias(member)
                      && std::meta::is_class_type(member)) {
            using Policy = [:member:];
            if constexpr (std::derived_from<Policy, ::fixy::tags::secret_policy::secret_policy_base>
                          && !std::is_same_v<Policy, ::fixy::tags::secret_policy::secret_policy_base>) {
                if (::fixy::atom::discharge_mask_of(member) != ::fixy::atom::DischargeAxis::None) ++count;
            }
        }
    }
#pragma GCC diagnostic pop
    return count;
}

}  // namespace detail

// ---------------------------------------------------------------------
// Reading a binding.
//
// Each matcher reads the premises of the binding: the sealed record of
// fixy/Collision.h that the collision rules read.  The premises read the
// Security grade through the one closed relation that fixy/Atom.h declares
// beside the Security atoms: a carrier is classified on each channel, and a
// declassification on each channel that the mask of its policy does not
// name.  So an export policy leaves the value a secret where replay or a
// background crossing is concerned, and the replay policy leaves it a secret
// on the way out.  The premises read IO, Bg and an observable effect from the
// row of the binding, so a system call that lifts IO opens the same channel
// as a stated with<IO>.  Which effects count as observable is decided in
// foundation/effects/Effect.h, where the effect atoms are declared.

namespace detail {

// The compile-time text search that the checks of this header read
// lives in fixy/Axis.h, because Collision.h needs it too and sits below
// this header.
using ::fixy::detail::text_contains;

// Assembled one time per entry, in a translation unit that reads the
// text.  The full_diagnostic() of each entry below is a template, so a
// translation unit that includes this header and reads no text does not
// assemble it.
template <class Entry>
inline constexpr std::string_view full_diagnostic_v = []() consteval -> std::string_view {
    ::foundation::reflect::anchored_t<^^Entry, std::string> text{
        "fixy::fn<Type, Atoms...> [tier 5: NotInCorpus]: the binding matches the "
        "refused-combination corpus entry "};
    text += Entry::name;
    text += ".  ";
    text += Entry::cite();
    return std::define_static_string(text);
}();

}  // namespace detail

// ---------------------------------------------------------------------
// The entries.  Each is a diagnostic tag, so the surface in
// foundation/diag prints it and fixy/Insights.h can say more about it,
// and a matcher over the resolved grades.  The name is the class name,
// which the check file of this header pins, so the string the message
// carries and the type the instantiation trail names cannot drift.

struct classified_io_without_declassify final : ::foundation::diag::tag_base {
    static constexpr std::string_view name = "classified_io_without_declassify";
    static constexpr std::string_view description =
        "A classified value flows out of the program through an I/O channel, and no declassification "
        "policy licenses the export.";
    static constexpr std::string_view remediation =
        "Drop the IO atom, project Security below the classified carrier with atom::as_public or "
        "atom::as_unclassified, or name atom::declassify<Policy> with a policy whose mask licenses IO: "
        "AuditedLogging, WireSerialize or UserDisplay.";

    [[nodiscard]] static constexpr bool matches(const ::fixy::collision::rule_premises& premise) noexcept {
        return premise.classified_on_send && premise.row_io;
    }

    [[nodiscard]] static constexpr std::string_view cite() noexcept {
        return "Sabelfeld-Myers 2003 (after Volpano-Smith-Irvine 1996 type-system foundation) — implicit "
               "information flow: classified value flows out of the program via I/O without a "
               "declassification policy that licenses I/O.  Drop the IO effect, project Security to "
               "as_public / as_unclassified, or declassify under AuditedLogging, WireSerialize or UserDisplay, "
               "the policies whose mask names IO.";
    }

    template <class Self = classified_io_without_declassify>
    [[nodiscard]] static constexpr std::string_view full_diagnostic() noexcept {
        return detail::full_diagnostic_v<Self>;
    }
};

struct classified_bg_without_declassify final : ::foundation::diag::tag_base {
    static constexpr std::string_view name = "classified_bg_without_declassify";
    static constexpr std::string_view description =
        "A classified value crosses into a background-thread context, whose scheduling then depends on the "
        "secret, and no declassification policy licenses the crossing.";
    static constexpr std::string_view remediation =
        "Drop the Bg atom and run the body on the foreground thread, where scheduling is deterministic, or "
        "project Security below the classified carrier: atom::as_public or atom::as_unclassified.  No "
        "policy licenses the Bg channel, so a declassification does not either.";

    [[nodiscard]] static constexpr bool matches(const ::fixy::collision::rule_premises& premise) noexcept {
        return premise.classified_on_bg && premise.row_bg;
    }

    [[nodiscard]] static constexpr std::string_view cite() noexcept {
        return "Smith-Volpano 1998 / Sabelfeld-Sands 2000 / Hedin-Sabelfeld 2012 — concurrent information "
               "flow: classified value crosses into a background-thread context without a declassification "
               "policy; the spawn is itself a scheduler-observable event.  Drop the Bg effect OR project "
               "Security to a less restrictive level; no policy discharges Bg.";
    }

    template <class Self = classified_bg_without_declassify>
    [[nodiscard]] static constexpr std::string_view full_diagnostic() noexcept {
        return detail::full_diagnostic_v<Self>;
    }
};

struct staleness_secret_without_declassify final : ::foundation::diag::tag_base {
    static constexpr std::string_view name = "staleness_secret_without_declassify";
    static constexpr std::string_view description =
        "A secret is reachable through a stale-replay window, and no declassification policy discharges "
        "the Staleness axis, so the erasure the policy would require never happens.";
    static constexpr std::string_view remediation =
        "Name atom::declassify<tags::secret_policy::AuthorizedReplay>, the one shipped policy whose mask "
        "carries Staleness; or drop the stale_to<N> atom, so Staleness returns to Fresh; or project "
        "Security below the carrier.";

    [[nodiscard]] static constexpr bool matches(const ::fixy::collision::rule_premises& premise) noexcept {
        return premise.classified_on_staleness && premise.stale;
    }

    [[nodiscard]] static constexpr std::string_view cite() noexcept {
        return "Hunt-Sands 2008 'Just Forget It' (POPL) — PRIMARY: formalizes information-erasure semantics "
               "and shows a classified value reachable through a stale-replay window (stale_to<TauMax>) "
               "without a freshness-discharging declassification policy is semantically a FAILED erasure — "
               "data the policy would require be forgotten remains observable.  Supporting: "
               "Askarov-Hunt-Sabelfeld-Sands 2008 (ESORICS) formalizes the timing/replay channel as an "
               "information leak distinct from data-flow channels.  Supporting: Sabelfeld-Myers 2003 "
               "'Language-based information-flow security' (IEEE J. Sel. Areas) — the discharge MUST match "
               "the axis it authorizes; a policy for IO export does not discharge temporal replay.  "
               "(Orientation only: Sabelfeld-Sands 2009 'Declassification: dimensions and principles' names "
               "the 'when' dimension — a survey that identifies the axis, NOT a formalization of this "
               "pattern.)  Remediation: name atom::declassify<tags::secret_policy::AuthorizedReplay> (the "
               "only shipped policy whose discharge mask carries Staleness) OR drop the "
               "stale_to<N> atom (Staleness defaults to Fresh) OR project Security to a less restrictive "
               "level.  The other declassify policies (AuditedLogging / WireSerialize / HashForCompare / "
               "LengthOnly / UserDisplay) do NOT silence this matcher — their authority lies on IO / "
               "Security axes, not Staleness.";
    }

    template <class Self = staleness_secret_without_declassify>
    [[nodiscard]] static constexpr std::string_view full_diagnostic() noexcept {
        return detail::full_diagnostic_v<Self>;
    }
};

struct ghost_runtime_observable final : ::foundation::diag::tag_base {
    static constexpr std::string_view name = "ghost_runtime_observable";
    static constexpr std::string_view description =
        "A ghost binding, erased at codegen, claims an effect that requires emitted code: Alloc, IO, Block "
        "or Bg.";
    static constexpr std::string_view remediation =
        "Drop the ghost atom, so Usage returns to its linear strict pole, or drop the runtime-observable "
        "effects.  A declassification does not apply: this is a ghost-versus-runtime category error, not an "
        "information-flow channel.";

    // The observable set of P010 leaves Bg out, and this entry reads Bg too.
    // Effect.h makes Bg observable, so the two premises give each observable
    // effect of the row through the one reader of fixy/Collision.h.
    [[nodiscard]] static constexpr bool matches(const ::fixy::collision::rule_premises& premise) noexcept {
        return premise.ghost && (premise.row_observable || premise.row_bg);
    }

    [[nodiscard]] static constexpr std::string_view cite() noexcept {
        return "Filliâtre-Gondelman-Paskevich 2014 'The Spirit of Ghost Code' / Leino 2010 'Dafny' — "
               "ghost-state discipline: a binding engaging Usage=Ghost AND any runtime-observable effect "
               "(Alloc / IO / Block / Bg) is contradictory — ghost values are erased at compile time and "
               "cannot drive runtime presence.  Drop the ghost Usage atom OR drop the runtime-observable "
               "effects.  Declassify does not apply (this is a ghost-vs-runtime category error, not an "
               "information-flow channel).";
    }

    template <class Self = ghost_runtime_observable>
    [[nodiscard]] static constexpr std::string_view full_diagnostic() noexcept {
        return detail::full_diagnostic_v<Self>;
    }
};

struct internal_io_without_declassify final : ::foundation::diag::tag_base {
    static constexpr std::string_view name = "internal_io_without_declassify";
    static constexpr std::string_view description =
        "An org-internal value, below the classified carrier but above public, flows into an I/O sink with "
        "no declassification policy: a write-down.";
    static constexpr std::string_view remediation =
        "Drop the IO atom, project Security to atom::as_public or atom::as_unclassified, or replace "
        "atom::as_internal with atom::declassify<Policy> for a policy whose mask licenses IO: AuditedLogging, "
        "WireSerialize or UserDisplay.";

    // One axis carries one grade, so as_internal and a declassification
    // cannot both sit on Security, and no mask reaches this entry.  A
    // binding that exports an internal value names the policy instead of
    // as_internal, and classified_io_without_declassify reads that grade.
    [[nodiscard]] static constexpr bool matches(const ::fixy::collision::rule_premises& premise) noexcept {
        return premise.internal && premise.row_io;
    }

    [[nodiscard]] static constexpr std::string_view cite() noexcept {
        return "Bell-LaPadula 1973 / Volpano-Smith-Irvine 1996 / Sabelfeld-Myers 2003 — no-write-down for "
               "Internal tier: org-internal value flows into an I/O sink without a declassification policy.  "
               "Internal data is below the strict pole (Classified) but ABOVE Public — every "
               "non-Public→Public crossing requires audit-trail discharge.  Drop the IO effect, project "
               "Security to as_public / as_unclassified, or declassify under a policy whose mask names IO.";
    }

    template <class Self = internal_io_without_declassify>
    [[nodiscard]] static constexpr std::string_view full_diagnostic() noexcept {
        return detail::full_diagnostic_v<Self>;
    }
};

struct internal_bg_without_declassify final : ::foundation::diag::tag_base {
    static constexpr std::string_view name = "internal_bg_without_declassify";
    static constexpr std::string_view description =
        "An org-internal value crosses into a background-thread context, whose scheduling then depends on "
        "it, with no declassification policy: a concurrent write-down.";
    static constexpr std::string_view remediation =
        "Drop the Bg atom and run the body on the foreground thread, where scheduling is deterministic, or "
        "project Security to atom::as_public or atom::as_unclassified.  No policy licenses the Bg channel.";

    [[nodiscard]] static constexpr bool matches(const ::fixy::collision::rule_premises& premise) noexcept {
        return premise.internal && premise.row_bg;
    }

    [[nodiscard]] static constexpr std::string_view cite() noexcept {
        return "Bell-LaPadula 1973 / Smith-Volpano 1998 / Sabelfeld-Myers 2003 — concurrent no-write-down "
               "for Internal tier: org-internal value crosses into a background-thread context whose "
               "scheduling becomes Internal-tier-dependent.  Sequential IFC type systems are UNSOUND under "
               "concurrency (the spawn itself is a scheduler-observable event); Internal data is below the "
               "strict pole (Classified) but ABOVE Public — every non-Public crossing through a "
               "scheduler-observable channel requires audit-trail discharge.  Drop the Bg effect (run on the "
               "foreground thread where scheduling is deterministic) OR project Security to as_public / "
               "as_unclassified; no policy discharges Bg.";
    }

    template <class Self = internal_bg_without_declassify>
    [[nodiscard]] static constexpr std::string_view full_diagnostic() noexcept {
        return detail::full_diagnostic_v<Self>;
    }
};

// ---------------------------------------------------------------------
// The corpus, in the order the walk consults it.  A binding that
// matches more than one entry reports the first.  Its size is the
// tuple's size and nothing else: there is no literal to keep in step.

using Entries =
    std::tuple<classified_io_without_declassify, classified_bg_without_declassify, staleness_secret_without_declassify,
               ghost_runtime_observable, internal_io_without_declassify, internal_bg_without_declassify>;

inline constexpr std::size_t corpus_size = std::tuple_size_v<Entries>;

// ---------------------------------------------------------------------
// Every entry declared under this namespace is in the corpus.
//
// Entries is a hand list, because its order is the order the walk
// consults.  An entry declared under fixy::corpus and left out of it
// matches nothing and refuses nothing.  So this walk reads the
// declarations, and it answers with the name of each entry that Entries
// does not hold.  It reads each nested namespace and each complete class,
// at each depth.
//
// An entry is a class.  The walk cannot tell whether a specialization of a
// class template is an entry without an instantiation, and an
// instantiation can stop the build with a static assertion of the template
// (fixy/Atom.h says why beside every_atom_in_is_rostered_).  So the walk
// names each class template under fixy::corpus outside fixy::corpus::detail,
// which holds the helpers of the matchers.
//
// The Site parameter gives each caller a vantage point of its own, as in
// fixy/collision/Report.h.  std::meta::members_of answers as of the point
// where the walk is instantiated, and each caller passes its own tag type,
// so its call walks the namespace as its translation unit has it at that
// line.  The walk is O(declarations under the namespace × corpus size).

namespace detail {

// An entry is a diagnostic tag with a matcher over a pack.
template <class Candidate>
concept IsCorpusEntry = std::derived_from<Candidate, ::foundation::diag::tag_base>
                     && requires(const ::fixy::collision::rule_premises& premise) {
                            { Candidate::matches(premise) } -> std::same_as<bool>;
                        };

[[nodiscard]] consteval bool is_in_entries_(std::meta::info candidate) {
    for (const std::meta::info entry : std::meta::template_arguments_of(std::meta::dealias(^^Entries))) {
        if (entry == candidate) return true;
    }
    return false;
}

// The offenders under Scope, appended to offenders: each entry that
// Entries does not hold, and each class template outside
// fixy::corpus::detail.  Each name after the first follows ", ".  The walk
// recurses into each nested namespace and into each complete class.  It
// reads a class only when it is complete, so that a class that a
// translation unit declares and does not define stops nothing.
template <class Site, std::meta::info Scope, bool InDetail>
consteval void append_corpus_offenders_(std::string& offenders, bool& found) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto member : std::define_static_array(
                      static_cast<::foundation::reflect::anchored_t<^^Site, std::vector<std::meta::info>>>(
                          std::meta::members_of(Scope, std::meta::access_context::current())))) {
        bool is_offender = false;
        if constexpr (std::meta::is_namespace(member) && !std::meta::is_namespace_alias(member)) {
            append_corpus_offenders_<Site, member, InDetail || member == ^^::fixy::corpus::detail>(offenders, found);
        } else if constexpr (std::meta::is_class_template(member)) {
            is_offender = !InDetail;
        } else if constexpr (std::meta::is_type(member) && !std::meta::is_type_alias(member)
                             && std::meta::is_class_type(member) && std::meta::is_complete_type(member)) {
            using Candidate = [:member:];
            if constexpr (IsCorpusEntry<Candidate>) is_offender = !is_in_entries_(std::meta::dealias(member));
            append_corpus_offenders_<Site, member, InDetail>(offenders, found);
        }
        if (is_offender) {
            offenders += found ? ", " : "";
            offenders += std::meta::display_string_of(member);
            found = true;
        }
    }
#pragma GCC diagnostic pop
}

}  // namespace detail

// The diagnostic names each offender that the walk finds, so a reader gets
// the declaration and the one line to edit.  It is empty when each entry is
// joined and no class template stands outside fixy::corpus::detail.
template <class Site>
[[nodiscard]] consteval std::string_view corpus_join_diagnostic() {
    ::foundation::reflect::anchored_t<^^Site, std::string> message =
        "fixy/Corpus.h: these declarations under fixy::corpus refuse nothing: ";
    bool found = false;
    detail::append_corpus_offenders_<Site, ^^::fixy::corpus, false>(message, found);
    if (!found) return {};
    message += ".  Name each entry in the Entries tuple, at the place in the order where the walk must consult "
               "it.  Declare an entry as a class: the walk cannot tell whether a specialization of a class template "
               "is an entry, so it refuses a class template outside fixy::corpus::detail.";
    return std::define_static_string(message);
}

// The check of the join.  The assertion here names each offender, so a
// caller reads the call in a constant initializer and asserts nothing of
// its own.
template <class Site>
[[nodiscard]] consteval bool corpus_is_joined() {
    constexpr std::string_view diagnostic = corpus_join_diagnostic<Site>();
    static_assert(diagnostic.size() == 0, diagnostic);
    return true;
}

namespace detail {

// The answer of the matcher of an entry to the premises of a binding.  The
// matcher is a static member function that is not a template, and the read
// is a concept, so no unit can give an entry another answer.  The premises
// are a structural value, so they can be an argument of the concept.
template <class Entry, ::fixy::collision::rule_premises Premises>
concept MatchesPremises = Entry::matches(Premises);

// The first entry of the list whose matcher holds of the premises, as a
// reflection, or the reflection of void when none does.  void is not an
// entry, so it cannot collide with a real answer.  The function is not a
// template, so no unit can specialize it.  The gate gives it the list
// Entries.  Complexity: the size of the corpus.
[[nodiscard]] consteval std::meta::info first_match_of_(const ::fixy::collision::rule_premises& premise,
                                                        std::meta::info entries) {
    const std::meta::info list = std::meta::dealias(entries);
    const std::size_t count = ::fixy::refined::argument_count_of(list);
    for (std::size_t index = 0; index < count; ++index) {
        const std::meta::info entry = ::fixy::refined::argument_of(list, index);
        if (std::meta::extract<bool>(
                std::meta::substitute(^^MatchesPremises, {entry, std::meta::reflect_constant(premise)})))
            return entry;
    }
    return ^^void;
}

}  // namespace detail

// True when the binding matches an entry of the corpus.  The read is a
// concept over the sealed premises of the binding and the closed matchers of
// the entries, so no unit can change an answer.
template <class Type, class... Atoms>
concept IsInCorpus =
    detail::first_match_of_(::fixy::collision::read_verdict(::fixy::collision::verdict_cell_v<Type, Atoms...>,
                                                            ^^::fixy::collision::verdict_key<Type, Atoms...>)
                                .premises,
                            ^^Entries)
    != ^^void;

// The entry a refused pack matched, or void when none did.
template <class Type, class... Atoms>
using matched_entry_or_void_t = [:detail::first_match_of_(
                                      ::fixy::collision::read_verdict(::fixy::collision::verdict_cell_v<Type, Atoms...>,
                                                                      ^^::fixy::collision::verdict_key<Type, Atoms...>)
                                          .premises,
                                      ^^Entries):];

namespace detail {

template <class Type, class... Atoms>
[[nodiscard]] consteval std::string_view entry_name_for_() noexcept {
    using Entry = matched_entry_or_void_t<Type, Atoms...>;
    if constexpr (std::is_void_v<Entry>) {
        return {};
    } else {
        return Entry::name;
    }
}

template <class Type, class... Atoms>
[[nodiscard]] consteval std::string_view full_diagnostic_for_() noexcept {
    using Entry = matched_entry_or_void_t<Type, Atoms...>;
    if constexpr (std::is_void_v<Entry>) {
        return {};
    } else {
        return Entry::full_diagnostic();
    }
}

}  // namespace detail

// Both are empty when no entry matches.  A caller never sees that case
// through the gate, because a binding with no match is never refused.
template <class Type, class... Atoms>
inline constexpr std::string_view corpus_entry_name_for_v = detail::entry_name_for_<Type, Atoms...>();

template <class Type, class... Atoms>
inline constexpr std::string_view corpus_full_diagnostic_v = detail::full_diagnostic_for_<Type, Atoms...>();

}  // namespace fixy::corpus
