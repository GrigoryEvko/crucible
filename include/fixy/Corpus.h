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
// A policy licenses exactly the channels its mask names.  A binding that
// names declassify<Policy> stays classified on every other channel, so a
// policy written for one export does not open another.  AuthorizedReplay
// licenses Staleness.  AuditedLogging, WireSerialize and UserDisplay
// license IO, because each names an export: a log, a wire and a display.
// HashForCompare and LengthOnly release a value derived from the secret
// and name no channel.  No policy licenses Bg: a crossing into a
// background context makes the scheduling depend on the value, and a
// release of the value does not license that.
//
// Over grades, not over the pack
// ------------------------------
// Each matcher reads collision::grades<Atoms...>::on<Axis> (Collision.h),
// the same resolved grade fn::grade_on gives, computed from the pack
// alone so that no entry completes fn and recurses through the gate.
// The effect row is read one step further: an entry that reads IO, Bg or
// an observable effect reads the row of the binding, which joins the
// Effect grade with the lifts of the atoms.
//
// One axis carries one grade, so "classified on a channel" is one
// question about the Security grade and the channel: the grade is a
// classified carrier, or it is a declassification whose mask does not
// name the channel.

#include <fixy/Atom.h>
#include <fixy/Axis.h>
#include <fixy/Collision.h>
#include <fixy/Tags.h>
#include <foundation/Platform.h>
#include <foundation/diag/Catalog.h>
#include <foundation/effects/Effect.h>
#include <foundation/effects/Row.h>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <meta>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

namespace fixy::corpus {

// ---------------------------------------------------------------------
// What a declassification discharges.
//
// A declassification policy authorizes exactly the axis it names.  The
// information-erasure literature is explicit on this: a policy written
// to authorize an export channel says nothing about temporal replay,
// so it must not silence a replay reject.  Hence the per-axis mask
// rather than a single "some declassify is present" test.

enum class DischargeAxis : std::uint32_t {
    None = 0u,
    Staleness = 1u << 0,
    IO = 1u << 1,
    Bg = 1u << 2,
};

[[nodiscard]] constexpr DischargeAxis operator|(DischargeAxis a, DischargeAxis b) noexcept {
    return DischargeAxis{std::to_underlying(a) | std::to_underlying(b)};
}
[[nodiscard]] constexpr DischargeAxis operator&(DischargeAxis a, DischargeAxis b) noexcept {
    return DischargeAxis{std::to_underlying(a) & std::to_underlying(b)};
}
[[nodiscard]] constexpr bool discharge_axis_contains(DischargeAxis mask, DischargeAxis axis) noexcept {
    return (std::to_underlying(mask) & std::to_underlying(axis)) != 0u;
}

// The mask of each policy, as one closed function.  A function that is
// not a template has no specialization, so no other file can widen the
// mask of a shipped policy.  A trait or a variable template would be
// open: an explicit specialization in any translation unit changes the
// answer that every later read in that unit sees.  A type that the
// function does not name licenses nothing, so a policy added to
// fixy/Tags.h licenses nothing until it is named here.
//
// AuthorizedReplay licenses the replay window.  The three exports each
// name an output channel, and IO is the channel of each: a log line, a
// serialized frame on a wire, and a rendered display.
[[nodiscard]] consteval DischargeAxis discharge_mask_of(std::meta::info policy) noexcept {
    const std::meta::info named = std::meta::dealias(policy);
    if (named == ^^::fixy::tags::secret_policy::AuthorizedReplay) return DischargeAxis::Staleness;
    if (named == ^^::fixy::tags::secret_policy::AuditedLogging) return DischargeAxis::IO;
    if (named == ^^::fixy::tags::secret_policy::WireSerialize) return DischargeAxis::IO;
    if (named == ^^::fixy::tags::secret_policy::UserDisplay) return DischargeAxis::IO;
    return DischargeAxis::None;
}

static_assert(discharge_mask_of(^^::fixy::tags::secret_policy::AuthorizedReplay) == DischargeAxis::Staleness,
              "AuthorizedReplay must discharge Staleness and nothing else.  It is the only policy that licenses "
              "the replay window, so a change here removes the sole discharge path for the staleness reject.");
static_assert(discharge_mask_of(^^::fixy::tags::secret_policy::AuditedLogging) == DischargeAxis::IO,
              "AuditedLogging licenses the log line it writes, which goes out through IO, and nothing else.");
static_assert(discharge_mask_of(^^::fixy::tags::secret_policy::WireSerialize) == DischargeAxis::IO,
              "WireSerialize licenses the serialized frame on the wire, which goes out through IO, and nothing "
              "else.");
static_assert(discharge_mask_of(^^::fixy::tags::secret_policy::UserDisplay) == DischargeAxis::IO,
              "UserDisplay licenses the rendered display, which goes out through IO, and nothing else.");
static_assert(discharge_mask_of(^^::fixy::tags::secret_policy::HashForCompare) == DischargeAxis::None,
              "HashForCompare releases a hash of the value and names no channel.  A binding that sends the hash "
              "out needs an export policy.");
static_assert(discharge_mask_of(^^::fixy::tags::secret_policy::LengthOnly) == DischargeAxis::None,
              "LengthOnly releases only size metadata and names no channel.  Size is an information channel, but "
              "no axis in DischargeAxis names it.");
static_assert(discharge_mask_of(^^int) == DischargeAxis::None, "a type that is not a policy licenses nothing");

namespace detail {

// A policy that licenses a channel is a decision a reviewer must see.
// This walks the policy namespace of fixy/Tags.h rather than a hand
// list, so the count reads every declared policy; fixy/Secret.h proves
// the declared set and the admitted set are one set.
[[nodiscard]] consteval std::size_t non_none_policy_count_() noexcept {
    std::size_t count = 0;
    static constexpr auto members = std::define_static_array(
        std::meta::members_of(^^::fixy::tags::secret_policy, std::meta::access_context::unchecked()));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto member : members) {
        if constexpr (std::meta::is_type(member) && !std::meta::is_type_alias(member)
                      && std::meta::is_class_type(member)) {
            using Policy = [:member:];
            if constexpr (std::derived_from<Policy, ::fixy::tags::secret_policy::secret_policy_base>
                          && !std::is_same_v<Policy, ::fixy::tags::secret_policy::secret_policy_base>) {
                if (discharge_mask_of(member) != DischargeAxis::None) ++count;
            }
        }
    }
#pragma GCC diagnostic pop
    return count;
}

}  // namespace detail

static_assert(detail::non_none_policy_count_() == 4,
              "Exactly four secret_policy tags license a channel: AuthorizedReplay for Staleness, and "
              "AuditedLogging, WireSerialize and UserDisplay for IO.  A policy that licenses a channel needs a "
              "line in discharge_mask_of, a pin that names the channel, and a change of this count.");

// ---------------------------------------------------------------------
// Reading a grade.

namespace detail {

// The grade on one axis, resolved from the pack alone.
template <Axis A, class... Atoms>
using grade_on = typename ::fixy::collision::grades<Atoms...>::template on<A>;

// The row of the binding: the Effect grade joined with the lifts of the
// atoms, through the relation of fixy/Atom.h that fixy/Fn.h and the
// collision rules also read.  An entry that reads IO, Bg or an observable
// effect reads this row, so a system call that lifts IO opens the same
// channel as a stated with<IO>.
template <class... Atoms>
using binding_row_on = ::fixy::atom::binding_row_of_t<grade_on<Axis::Effect, Atoms...>, Atoms...>;

// Whether a declassification licenses one channel: the mask of its
// policy names the channel.  A grade that is not a declassification
// licenses nothing.
template <DischargeAxis Channel, class G>
struct can_discharge_ : std::false_type {};
template <DischargeAxis Channel, class Policy>
struct can_discharge_<Channel, ::fixy::atom::declassify<Policy>>
    : std::bool_constant<discharge_axis_contains(discharge_mask_of(^^Policy), Channel)> {};

// The two Security readings, each a reading of the one closed relation
// fixy/Atom.h declares beside the Security atoms.  A type that is not a
// Security grade makes no Security claim, so each answers false for it.
// Every Security atom IS a Security grade, which fixy/Atom.h's roster
// walk proves, so a new point on the axis cannot fall through to that
// false answer and pass as public.

// Whether the value is classified on one channel.  A classified carrier
// is classified on every channel: the strict pole counts, because a
// binding that says nothing about Security is classified, and
// constant_time counts, because it states the classification too.  A
// declassification is classified on each channel that its mask does not
// name.  So an export policy leaves the value a secret where replay or a
// background crossing is concerned, and the replay policy leaves it a
// secret on the way out.
template <DischargeAxis Channel, class G>
struct is_classified_on_ : std::false_type {};
template <DischargeAxis Channel, ::fixy::atom::IsSecurityGrade G>
struct is_classified_on_<Channel, G>
    : std::bool_constant<::fixy::atom::is_classified_carrier_v<G>
                         || (::fixy::atom::security_class_of_v<G> == ::fixy::atom::SecurityClass::Declassified
                             && !can_discharge_<Channel, G>::value)> {};

// The Internal tier sits below the strict pole, so a binding reaches it
// only by writing as_internal.  It carries no declassification, so no
// mask reaches it.
template <class G>
struct is_internal_ : std::false_type {};
template <::fixy::atom::IsSecurityGrade G>
struct is_internal_<G>
    : std::bool_constant<::fixy::atom::security_class_of_v<G> == ::fixy::atom::SecurityClass::Internal> {};

// Which effects count as observable is decided where the effect atoms
// are declared, not here.  Deferring keeps a newly added atom from
// defaulting to unobservable, which would widen what ghost code is
// allowed to request without anyone deciding to widen it.  The entry
// reads the row of the binding, which binding_row_on above gives as a
// canonical Row.
template <class R>
struct is_row_observable_ : std::false_type {};
template <::foundation::effects::Effect... Es>
struct is_row_observable_<::foundation::effects::Row<Es...>>
    : std::bool_constant<(::foundation::effects::is_observable<Es>() || ...)> {};

static_assert(!is_row_observable_<::foundation::effects::Row<::foundation::effects::Effect::Init>>::value,
              "Init must stay outside the observable set.  Moving it in rejects every ghost binding that "
              "participates in initialization, so it needs its own corpus entry naming the contradiction it "
              "catches.");
static_assert(!is_row_observable_<::foundation::effects::Row<::foundation::effects::Effect::Test>>::value,
              "Test must stay outside the observable set.  A specification evaluated under a test harness is "
              "legitimate ghost code.");
static_assert(is_row_observable_<::foundation::effects::Row<::foundation::effects::Effect::Alloc>>::value,
              "Alloc must stay inside the observable set.");
static_assert(is_row_observable_<::foundation::effects::Row<::foundation::effects::Effect::IO>>::value,
              "IO must stay inside the observable set.");
static_assert(is_row_observable_<::foundation::effects::Row<::foundation::effects::Effect::Block>>::value,
              "Block must stay inside the observable set.");
static_assert(is_row_observable_<::foundation::effects::Row<::foundation::effects::Effect::Bg>>::value,
              "Bg must stay inside the observable set.");

template <class G>
struct is_stale_ : std::false_type {};
template <auto TauMax>
struct is_stale_<::fixy::atom::stale_to<TauMax>> : std::true_type {};

template <class G>
struct is_ghost_ : std::false_type {};
template <>
struct is_ghost_<::fixy::atom::ghost> : std::true_type {};

// The compile-time text search these self-tests read lives in
// fixy/Axis.h, because Collision.h needs it too and sits below this
// header.
using ::fixy::detail::text_contains;

// Assembled once per entry.  A function-local static inside each entry
// would be per translation unit and re-run the assembly in every
// including unit; keyed on the entry, one instantiation serves the
// build.
template <class Entry>
inline constexpr std::string_view full_diagnostic_v = []() consteval -> std::string_view {
    std::string text{"fixy::fn<Type, Atoms...> [tier 5: NotInCorpus]: the binding matches the "
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
// which the self-test at the foot pins, so the string the message
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

    template <class Type, class... Atoms>
    [[nodiscard]] static consteval bool matches() noexcept {
        using Security = detail::grade_on<Axis::Security, Atoms...>;
        using Effects = detail::binding_row_on<Atoms...>;
        const bool classified_on_io = detail::is_classified_on_<DischargeAxis::IO, Security>::value;
        const bool has_io = ::foundation::effects::row_contains_v<Effects, ::foundation::effects::Effect::IO>;
        return classified_on_io && has_io;
    }

    [[nodiscard]] static constexpr std::string_view cite() noexcept {
        return "Sabelfeld-Myers 2003 (after Volpano-Smith-Irvine 1996 type-system foundation) — implicit "
               "information flow: classified value flows out of the program via I/O without a "
               "declassification policy that licenses I/O.  Drop the IO effect, project Security to "
               "as_public / as_unclassified, or declassify under AuditedLogging, WireSerialize or UserDisplay, "
               "the policies whose mask names IO.";
    }

    [[nodiscard]] static constexpr std::string_view full_diagnostic() noexcept {
        return detail::full_diagnostic_v<classified_io_without_declassify>;
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

    template <class Type, class... Atoms>
    [[nodiscard]] static consteval bool matches() noexcept {
        using Security = detail::grade_on<Axis::Security, Atoms...>;
        using Effects = detail::binding_row_on<Atoms...>;
        const bool classified_on_bg = detail::is_classified_on_<DischargeAxis::Bg, Security>::value;
        const bool has_bg = ::foundation::effects::row_contains_v<Effects, ::foundation::effects::Effect::Bg>;
        return classified_on_bg && has_bg;
    }

    [[nodiscard]] static constexpr std::string_view cite() noexcept {
        return "Smith-Volpano 1998 / Sabelfeld-Sands 2000 / Hedin-Sabelfeld 2012 — concurrent information "
               "flow: classified value crosses into a background-thread context without a declassification "
               "policy; the spawn is itself a scheduler-observable event.  Drop the Bg effect OR project "
               "Security to a less restrictive level; no policy discharges Bg.";
    }

    [[nodiscard]] static constexpr std::string_view full_diagnostic() noexcept {
        return detail::full_diagnostic_v<classified_bg_without_declassify>;
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

    template <class Type, class... Atoms>
    [[nodiscard]] static consteval bool matches() noexcept {
        using Security = detail::grade_on<Axis::Security, Atoms...>;
        using Staleness = detail::grade_on<Axis::Staleness, Atoms...>;
        const bool classified_on_replay = detail::is_classified_on_<DischargeAxis::Staleness, Security>::value;
        const bool has_stale = detail::is_stale_<Staleness>::value;
        return classified_on_replay && has_stale;
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

    [[nodiscard]] static constexpr std::string_view full_diagnostic() noexcept {
        return detail::full_diagnostic_v<staleness_secret_without_declassify>;
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

    template <class Type, class... Atoms>
    [[nodiscard]] static consteval bool matches() noexcept {
        using Usage = detail::grade_on<Axis::Usage, Atoms...>;
        using Effects = detail::binding_row_on<Atoms...>;
        const bool has_ghost = detail::is_ghost_<Usage>::value;
        const bool has_observable = detail::is_row_observable_<Effects>::value;
        return has_ghost && has_observable;
    }

    [[nodiscard]] static constexpr std::string_view cite() noexcept {
        return "Filliâtre-Gondelman-Paskevich 2014 'The Spirit of Ghost Code' / Leino 2010 'Dafny' — "
               "ghost-state discipline: a binding engaging Usage=Ghost AND any runtime-observable effect "
               "(Alloc / IO / Block / Bg) is contradictory — ghost values are erased at compile time and "
               "cannot drive runtime presence.  Drop the ghost Usage atom OR drop the runtime-observable "
               "effects.  Declassify does not apply (this is a ghost-vs-runtime category error, not an "
               "information-flow channel).";
    }

    [[nodiscard]] static constexpr std::string_view full_diagnostic() noexcept {
        return detail::full_diagnostic_v<ghost_runtime_observable>;
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
    template <class Type, class... Atoms>
    [[nodiscard]] static consteval bool matches() noexcept {
        using Security = detail::grade_on<Axis::Security, Atoms...>;
        using Effects = detail::binding_row_on<Atoms...>;
        const bool has_internal = detail::is_internal_<Security>::value;
        const bool has_io = ::foundation::effects::row_contains_v<Effects, ::foundation::effects::Effect::IO>;
        return has_internal && has_io;
    }

    [[nodiscard]] static constexpr std::string_view cite() noexcept {
        return "Bell-LaPadula 1973 / Volpano-Smith-Irvine 1996 / Sabelfeld-Myers 2003 — no-write-down for "
               "Internal tier: org-internal value flows into an I/O sink without a declassification policy.  "
               "Internal data is below the strict pole (Classified) but ABOVE Public — every "
               "non-Public→Public crossing requires audit-trail discharge.  Drop the IO effect, project "
               "Security to as_public / as_unclassified, or declassify under a policy whose mask names IO.";
    }

    [[nodiscard]] static constexpr std::string_view full_diagnostic() noexcept {
        return detail::full_diagnostic_v<internal_io_without_declassify>;
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

    template <class Type, class... Atoms>
    [[nodiscard]] static consteval bool matches() noexcept {
        using Security = detail::grade_on<Axis::Security, Atoms...>;
        using Effects = detail::binding_row_on<Atoms...>;
        const bool has_internal = detail::is_internal_<Security>::value;
        const bool has_bg = ::foundation::effects::row_contains_v<Effects, ::foundation::effects::Effect::Bg>;
        return has_internal && has_bg;
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

    [[nodiscard]] static constexpr std::string_view full_diagnostic() noexcept {
        return detail::full_diagnostic_v<internal_bg_without_declassify>;
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
// Every entry declared in this namespace is in the corpus.
//
// Entries is a hand list, because its order is the order the walk
// consults.  An entry declared in fixy::corpus and left out of it
// matches nothing and refuses nothing.  So this walk reads the declarations,
// and it answers with the name of each entry that Entries does not hold.
//
// The Site parameter gives each caller a vantage point of its own, as in
// fixy/Collision.h.  std::meta::members_of answers as of the point where
// the walk is instantiated, and each caller passes its own tag type, so
// its call walks the namespace as its translation unit has it at that
// line.  The walk is O(members of the namespace × corpus size).

namespace detail {

// An entry is a diagnostic tag with a matcher over a pack.
template <class Candidate>
concept IsCorpusEntry = std::derived_from<Candidate, ::foundation::diag::tag_base> && requires {
    { Candidate::template matches<int>() } -> std::same_as<bool>;
};

[[nodiscard]] consteval bool is_in_entries_(std::meta::info candidate) {
    for (const std::meta::info entry : std::meta::template_arguments_of(std::meta::dealias(^^Entries))) {
        if (entry == candidate) return true;
    }
    return false;
}

}  // namespace detail

template <class Site>
[[nodiscard]] consteval std::string corpus_entries_declared_but_not_joined() {
    std::string offenders;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto member : std::define_static_array(
                      std::meta::members_of(^^::fixy::corpus, std::meta::access_context::current()))) {
        if constexpr (std::meta::is_type(member)) {
            using Candidate = [:member:];
            if constexpr (detail::IsCorpusEntry<Candidate>) {
                if (!detail::is_in_entries_(std::meta::dealias(member))) {
                    if (!offenders.empty()) offenders += ", ";
                    offenders += std::meta::identifier_of(member);
                }
            }
        }
    }
#pragma GCC diagnostic pop
    return offenders;
}

// The diagnostic names each entry that the walk finds, so a reader gets
// the entry and the one line to edit.  It is empty when every entry is
// joined.
template <class Site>
[[nodiscard]] consteval std::string_view corpus_join_diagnostic() {
    const std::string offenders = corpus_entries_declared_but_not_joined<Site>();
    if (offenders.empty()) return {};
    std::string message =
        "fixy/Corpus.h: these corpus entries are declared in fixy::corpus and missing from Entries, so they "
        "refuse nothing: ";
    message += offenders;
    message += ".  Name each one in the Entries tuple, at the place in the order where the walk must consult it.";
    return std::define_static_string(message);
}

namespace detail {
struct corpus_header_site_ final {};
}  // namespace detail

static_assert(corpus_entries_declared_but_not_joined<detail::corpus_header_site_>().empty(),
              corpus_join_diagnostic<detail::corpus_header_site_>());

namespace detail {

// The first entry the pack matches, as a reflection, or the reflection
// of void when none does.  void is not an entry, so it cannot collide
// with a real answer.
template <class Type, class... Atoms>
[[nodiscard]] consteval std::meta::info first_match_() noexcept {
    std::meta::info found = ^^void;
    static constexpr auto entries =
        std::define_static_array(std::meta::template_arguments_of(std::meta::dealias(^^Entries)));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto entry : entries) {
        using Entry = [:entry:];
        // The comparison is parenthesised because `^^void && x` lexes as
        // the reflection of the type void&&.
        if ((found == ^^void)&&Entry::template matches<Type, Atoms...>()) {
            found = entry;
        }
    }
#pragma GCC diagnostic pop
    return found;
}

}  // namespace detail

template <class Type, class... Atoms>
[[nodiscard]] consteval bool is_in_corpus() noexcept {
    return detail::first_match_<Type, Atoms...>() != ^^void;
}

template <class Type, class... Atoms>
inline constexpr bool is_in_corpus_v = is_in_corpus<Type, Atoms...>();

// The entry a refused pack matched, or void when none did.
template <class Type, class... Atoms>
using matched_entry_or_void_t = [:detail::first_match_<Type, Atoms...>():];

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

// ---------------------------------------------------------------------
// The header proves its own claims here.

namespace detail::corpus_self_test {

// Every entry is a diagnostic tag whose name is its own class name, so
// the string the message carries cannot drift from the type the
// instantiation trail names.  Each carries a citation, and its full
// diagnostic names it.
template <class Entry>
[[nodiscard]] consteval bool entry_is_well_formed_() noexcept {
    return ::foundation::diag::is_diagnostic_class_v<Entry> && Entry::name == std::meta::identifier_of(^^Entry)
        && !Entry::description.empty() && !Entry::remediation.empty() && !Entry::cite().empty()
        && Entry::full_diagnostic().starts_with("fixy::fn<") && text_contains(Entry::full_diagnostic(), Entry::name)
        && text_contains(Entry::full_diagnostic(), Entry::cite());
}

[[nodiscard]] consteval bool every_entry_is_well_formed_() noexcept {
    bool all_well_formed = true;
    static constexpr auto entries =
        std::define_static_array(std::meta::template_arguments_of(std::meta::dealias(^^Entries)));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto entry : entries) {
        using Entry = [:entry:];
        all_well_formed = all_well_formed && entry_is_well_formed_<Entry>();
    }
#pragma GCC diagnostic pop
    return all_well_formed;
}

static_assert(every_entry_is_well_formed_(),
              "fixy/Corpus.h: a corpus entry is not a diagnostic tag, or its name is not its class name, or "
              "one of its texts is empty, or its full diagnostic does not carry its name and its citation.");

// The shortest binding is not in the corpus: every axis at its strict
// pole names no effect and no replay window.
static_assert(!is_in_corpus_v<int>);
static_assert(std::is_same_v<matched_entry_or_void_t<int>, void>);

// Reject by default on the Security axis.  An IO row with no Security
// atom is a classified value on an observable channel, and is refused;
// the same pack naming the public grade is admitted.
static_assert(is_in_corpus_v<int, ::fixy::atom::with_io>);
static_assert(std::is_same_v<matched_entry_or_void_t<int, ::fixy::atom::with_io>, classified_io_without_declassify>);
static_assert(!is_in_corpus_v<int, ::fixy::atom::with_io, ::fixy::atom::as_public>);

// A declassification licenses the channels its policy names, and no
// other.  The wire policy licenses IO.  The replay policy does not, so
// the same IO row under it is classified IO.  No policy licenses Bg.
static_assert(
    !is_in_corpus_v<int, ::fixy::atom::with_io, ::fixy::atom::declassify<::fixy::tags::secret_policy::WireSerialize>>);
static_assert(
    std::is_same_v<matched_entry_or_void_t<int, ::fixy::atom::with_io,
                                           ::fixy::atom::declassify<::fixy::tags::secret_policy::AuthorizedReplay>>,
                   classified_io_without_declassify>);
static_assert(
    std::is_same_v<matched_entry_or_void_t<int, ::fixy::atom::with_bg,
                                           ::fixy::atom::declassify<::fixy::tags::secret_policy::AuditedLogging>>,
                   classified_bg_without_declassify>);

// An export policy leaves the value a secret where replay is concerned,
// and only the replay policy discharges the axis.
static_assert(is_in_corpus_v<int, ::fixy::atom::declassify<::fixy::tags::secret_policy::AuditedLogging>,
                             ::fixy::atom::stale_to<5>>);
static_assert(!is_in_corpus_v<int, ::fixy::atom::declassify<::fixy::tags::secret_policy::AuthorizedReplay>,
                              ::fixy::atom::stale_to<5>>);

// The name and the diagnostic read off a matched pack, and are empty
// off an admitted one.
static_assert(corpus_entry_name_for_v<int, ::fixy::atom::with_io> == "classified_io_without_declassify");
static_assert(text_contains(corpus_full_diagnostic_v<int, ::fixy::atom::with_io>, "Sabelfeld-Myers 2003"));
static_assert(corpus_entry_name_for_v<int>.empty());
static_assert(corpus_full_diagnostic_v<int>.empty());

}  // namespace detail::corpus_self_test

}  // namespace fixy::corpus
