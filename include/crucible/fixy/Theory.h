#pragma once

// ── crucible::fixy — Theory.h — §30.14 known-unsoundness corpus ────
//
// Phase D of the fixy reimplementation per misc/16_05_2026_fixy.md
// §4.  This is the LOAD-BEARING reject-by-default surface that closes
// the FX §30.14 type-theory unsoundness corpus.  Currently ships
// FOUR entries — classified_io_without_declassify,
// classified_bg_without_declassify, staleness_secret_without_declassify,
// and ghost_runtime_observable — each pairs:
//
//   (a) a named pattern detector — a constexpr predicate over
//       (Type, Grants...) that returns true iff the binding matches
//       a known-broken shape from the literature;
//
//   (b) a doc-comment citation (paper, year, mechanism) explaining
//       WHY the pattern is unsound + which substrate primitive
//       remediates it.
//
// The corpus is DATA, not code.  New entries are 10-line additions
// to the `corpus::` namespace; the closed-set `IsInUnsoundnessCorpus`
// disjunction picks them up automatically via fold-expression scan.
//
// ── Strict-default-Security coverage (fixy-CR-01) ──────────────────
//
// The corpus detects Security engagement via `is_secret_grant<G>`, a
// closed-set predicate that recognizes THREE canonical Security tag
// shapes:
//   1. `grant::as_secret`                                 (= Secret)
//   2. `grant::as_classified`                             (= Classified)
//   3. `grant::accept_default_strict_for<Security>`       (= Classified
//                                                            via the
//                                                            strict-default
//                                                            projection)
// Shape #3 is the load-bearing addition closing the fixy-CR-01 bypass:
// production stances that use `strict<Security>` (the implicit-secret
// form) are caught by the corpus just like explicit-as_classified
// bindings.  The invariant `strict_default_for<Security>::value ==
// SecLevel::Classified` is locked by a static_assert sentinel — if
// the strict default is ever weakened below Classified, the build
// breaks and the predicate's coverage needs re-evaluation.
//
// Internal-tier IFC (`as_internal + with<IO>`) is OUT OF SCOPE for
// this corpus entry — see fixy-H-18 for the separate Internal-tier
// channel discussion.
//
// ── Substrate consumed ─────────────────────────────────────────────
//
//   fixy/Grant.h — grant tag types (as_secret, declassify, with<>...)
//   fixy/Reject.h — IsAccepted gate that this header extends
//
// ── How the gate fires ─────────────────────────────────────────────
//
// `IsAccepted<Type, Grants...>` (Reject.h, §IsAccepted full gate)
// composes engagement + well-formedness + uniqueness +
// `NotInTheoryCorpus<Type, Grants...>` (this header).  A binding
// that engages every axis correctly STILL fails IsAccepted if its
// Grants pack matches any §30.14 entry.  The fixy-level diagnostic
// names which corpus entry matched (struct name + paper + year) —
// the tier-5 static_assert keyed by `fixy_h02_tier5_not_in_corpus`
// in fixy/Fn.h surfaces the matched entry's `name()` (e.g.
// `classified_io_without_declassify` —
// fixy-H-16) AND its `cite()` text (e.g. "Volpano-Smith-Irvine
// 1996 / Sabelfeld-Myers 2003 — …" — fixy-H-13) via
// `corpus_full_diagnostic_v<Type, Grants...>`, assembled into
// static storage by P3491R3 `std::define_static_string` and
// emitted via P2741R3 user-generated static_assert messages.  The
// rejection diagnostic literally IS "matched corpus entry: <name>
// — <cite + remediation>" rather than a generic
// "binding in corpus" pointer.
//
// ── Discipline ─────────────────────────────────────────────────────
//
// Adding a new corpus entry is one PR with:
//   1. A `corpus::<paper>_<year>::matches<Type, Grants...>()` predicate
//   2. A doc-comment explaining the unsoundness + remediation
//   3. A `theory_neg/` neg-compile fixture exercising it
//   4. Addition to `IsInUnsoundnessCorpus`'s OR fold
//
// The corpus grows monotonically — never delete entries.  Outdated
// patterns can be marked `[[deprecated]]` with a cite to the
// substrate-level fix that retired them.

#include <crucible/fixy/Default.h>
#include <crucible/fixy/Dim.h>
#include <crucible/fixy/Grant.h>
#include <crucible/safety/Fn.h>
#include <crucible/safety/Secret.h>          // fixy-A4-015: AuthorizedReplay et al.

#include <cstddef>
#include <cstdint>                           // fixy-A4-015: std::uint32_t for DischargeAxis
#include <meta>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>                           // fixy-A4-015: std::to_underlying

namespace crucible::fixy::theory {

// ═════════════════════════════════════════════════════════════════════
// ── Detection helpers — predicates over a Grants pack ──────────────
// ═════════════════════════════════════════════════════════════════════

namespace detail {

// `has_grant_of<Predicate, Grants...>` — true iff any grant in the
// pack matches the per-grant boolean trait `Predicate<G>::value`.
template <template <typename> class Predicate, typename... Grants>
[[nodiscard]] consteval bool has_grant_of() noexcept {
    if constexpr (sizeof...(Grants) == 0) {
        return false;
    } else {
        return (Predicate<Grants>::value || ...);
    }
}

// Per-tag boolean traits — true for the matching shape, false else.

template <typename G> struct is_secret_grant
    : std::false_type {};
template <> struct is_secret_grant<grant::as_secret>
    : std::true_type {};
template <> struct is_secret_grant<grant::as_classified>
    : std::true_type {};
// fixy-A4-015: declassify<Policy> ALSO engages the Security axis
// (per Grant.h `which_dim_v<declassify<P>> == DimensionAxis::Security`).
// Pre-A4-015 this specialization was absent — packs that engaged
// Security ONLY via declassify (no as_secret / as_classified /
// strict<Security>) bypassed every corpus entry that gates on
// `has_secret`.  This is the canonical Hunt-Sands bypass that
// A4-015 closes: the matcher now sees the declassify as a Security
// engagement AND consults `has_declassify_for_axis<Axis>` to decide
// whether the discharge is axis-appropriate.  Existing fixtures that
// engage Security via as_secret / as_classified / strict-default
// REMAIN rejected (their behaviour is unchanged); only the
// declassify-only-Security path becomes structurally visible.
template <typename Policy>
struct is_secret_grant<grant::declassify<Policy>>
    : std::true_type {};

// Strict-default-Security form (fixy-CR-01).  The grant
// `accept_default_strict_for<Security>` is semantically equivalent to
// an explicit `as_classified` engagement — both resolve to
// `SecLevel::Classified` via `strict_default_for<Security>::value`.
// Without this specialization, the production stances
// `IoFunction<T>` / `BgWorker<T>` / `AsyncEndpoint<T>` (which use
// `strict<Security> + with_io|with_bg`) silently bypass the §30.14
// corpus despite being the exact implicit-flow pattern the corpus
// targets.  The static_assert below locks the invariant: if anyone
// weakens the strict default below `Classified`, the build breaks
// (and the predicate's semantic correctness needs re-evaluation).
template <>
struct is_secret_grant<grant::accept_default_strict_for<
        dim::DimensionAxis::Security>>
    : std::true_type {};

static_assert(
    strict_default_for<dim::DimensionAxis::Security>::value
        == ::crucible::safety::fn::SecLevel::Classified,
    "Theory.h §30.14 invariant: strict_default_for<Security> must "
    "resolve to SecLevel::Classified.  The strict-default-Security "
    "form of is_secret_grant relies on this equivalence; weakening "
    "the strict default would re-open the fixy-CR-01 bypass.");

template <typename G> struct is_declassify_grant
    : std::false_type {};
template <typename Policy>
struct is_declassify_grant<grant::declassify<Policy>>
    : std::true_type {};

// ── fixy-A4-015: per-axis declassify discipline ────────────────────
//
// Pre-A4-015 the §30.14 corpus treated `declassify<Policy>` as a
// universal silencer: any policy tag — even one whose semantic
// authority lies on an unrelated axis — was enough to silence ANY
// declassify-discharging corpus reject.  Hunt-Sands 2008 'Just Forget
// It' (POPL) and Sabelfeld-Myers 2003 'Language-based information-
// flow security' (IEEE J. Sel. Areas) both require that the
// declassification policy MATCH the axis it discharges: a policy
// intended for IO export (e.g. AuditedLogging / WireSerialize)
// authorizes the export channel and says NOTHING about temporal
// replay; using it to silence a `stale_to<N>` × `as_secret` reject
// is the canonical "wrong policy authorizes the wrong axis" footgun.
//
// `DischargeAxis` is a bitmask of axes a declassification policy may
// authoritatively discharge.  Each axis bit lifts ONE corpus matcher
// from "any declassify silences" to "only policies that explicitly
// admit this axis silence".  The default specialization of
// `axes_discharged_of<Policy>` returns `DischargeAxis::None` — the
// Hunt-Sands safe-default: unknown policies discharge nothing, so
// pre-existing declassify policies CANNOT silence newly-axis-typed
// corpus rejects (defends against an over-broad declassify becoming
// load-bearing for an axis its author never considered).
//
// Currently surfaced axes:
//   - Staleness  — Hunt-Sands erasure / replay-window discharge.
//                  Authoritative policy: `secret_policy::AuthorizedReplay`.
// Reserved (placeholder bits for future H-tasks that tighten the
// corresponding matchers; assigning bits here keeps the mask stable
// across the lifetime of any field that stores an `axes_discharged_of`
// value):
//   - IO         — Sabelfeld-Myers 2003 implicit-flow IO discharge
//                  (Volpano-Smith-Irvine 1996 type-system foundation;
//                  the IO-channel-specific reading is SM03's, not
//                  VSI96's — fixy-M-16 softened attribution).
//   - Bg         — Smith-Volpano 1998 concurrent-IFC scheduler discharge.
//   - Crash      — BSYZ22 crash-stop session-protocol audit discharge.
//   - Reentrancy — re-entrant audit-trail (re-classification) discharge.
enum class DischargeAxis : std::uint32_t {
    None       = 0u,
    Staleness  = 1u << 0,
    IO         = 1u << 1,  // reserved — see fixy-A4-015 Theory.h
    Bg         = 1u << 2,  // reserved
    Crash      = 1u << 3,  // reserved
    Reentrancy = 1u << 4,  // reserved
};

[[nodiscard]] constexpr DischargeAxis
operator|(DischargeAxis a, DischargeAxis b) noexcept {
    return DischargeAxis{std::to_underlying(a) | std::to_underlying(b)};
}
[[nodiscard]] constexpr DischargeAxis
operator&(DischargeAxis a, DischargeAxis b) noexcept {
    return DischargeAxis{std::to_underlying(a) & std::to_underlying(b)};
}
[[nodiscard]] constexpr bool
discharge_axis_contains(DischargeAxis mask, DischargeAxis axis) noexcept {
    return (std::to_underlying(mask) & std::to_underlying(axis)) != 0u;
}

// `axes_discharged_of<Policy>` — bitmask of axes the named policy is
// authoritative on.  Defaults to `DischargeAxis::None` per the Hunt-
// Sands safe-default rule (above).  Specialize per-policy to lift
// the bits the policy genuinely admits.
template <typename Policy>
struct axes_discharged_of
    : std::integral_constant<DischargeAxis, DischargeAxis::None> {};
template <typename Policy>
inline constexpr DischargeAxis axes_discharged_of_v =
    axes_discharged_of<Policy>::value;

// `secret_policy::AuthorizedReplay` discharges Staleness.  Defined in
// substrate `safety/Secret.h` (re-exported as
// `fixy::tags::secret_policy::AuthorizedReplay` via the namespace
// alias in fixy/Source.h).  This is THE policy that admits
// `as_secret + stale_to<N>` through the §30.14 corpus.
template <>
struct axes_discharged_of<
        ::crucible::safety::secret_policy::AuthorizedReplay>
    : std::integral_constant<DischargeAxis, DischargeAxis::Staleness> {};

// `is_declassify_for_axis<Axis, G>` — true iff G is a
// `grant::declassify<Policy>` AND that policy's
// `axes_discharged_of_v` mask contains `Axis`.
template <DischargeAxis Axis, typename G>
struct is_declassify_for_axis : std::false_type {};
template <DischargeAxis Axis, typename Policy>
struct is_declassify_for_axis<Axis, grant::declassify<Policy>>
    : std::bool_constant<
          discharge_axis_contains(axes_discharged_of_v<Policy>, Axis)> {};

// `has_declassify_for_axis<Axis, Grants...>()` — fold OR over the
// pack: does any grant carry a declassify<Policy> that discharges
// `Axis`?  Distinct from the unscoped `has_grant_of<...,
// is_declassify_grant, Grants...>` which silences on ANY declassify
// regardless of policy axis authority.
template <DischargeAxis Axis, typename... Grants>
[[nodiscard]] consteval bool has_declassify_for_axis() noexcept {
    if constexpr (sizeof...(Grants) == 0) {
        return false;
    } else {
        return (is_declassify_for_axis<Axis, Grants>::value || ...);
    }
}

// Locked invariants — if the substrate's AuthorizedReplay tag is
// renamed or removed, or its axis assignment shifts, these fire
// before any matcher silently breaks.
static_assert(
    axes_discharged_of_v<
        ::crucible::safety::secret_policy::AuthorizedReplay>
        == DischargeAxis::Staleness,
    "fixy-A4-015: AuthorizedReplay must discharge Staleness — "
    "the only currently-surfaced axis-specific policy in the §30.14 "
    "corpus.  Lifting other axes (IO/Bg/Crash/Reentrancy) requires "
    "naming new policy tags and specializing axes_discharged_of "
    "accordingly; existing AuditedLogging / WireSerialize / "
    "HashForCompare / LengthOnly / UserDisplay tags REMAIN at "
    "DischargeAxis::None per Hunt-Sands safe-default discipline.");
static_assert(
    axes_discharged_of_v<
        ::crucible::safety::secret_policy::AuditedLogging>
        == DischargeAxis::None,
    "fixy-A4-015: pre-A4-015 declassify policies retain the safe "
    "default DischargeAxis::None — any future axis lift requires an "
    "explicit specialization with Hunt-Sands-style justification at "
    "the policy tag's declaration site (substrate safety/Secret.h).");

template <typename G> struct is_io_effect_grant
    : std::false_type {};
template <effects::Effect... Es>
struct is_io_effect_grant<grant::with<Es...>>
    : std::bool_constant<((Es == effects::Effect::IO) || ...)> {};

// `is_internal_grant<G>` — true iff G is the `grant::as_internal`
// Security-engagement tag.  Used by the
// `internal_io_without_declassify` corpus entry (fixy-H-18) to detect
// org-internal data flowing into an I/O sink without an audit-
// discharging declassification policy.  Distinct from
// `is_secret_grant`: `as_internal` resolves to `SecLevel::Internal`
// (= 2), strictly below the strict-default `SecLevel::Classified`
// (= 3) that `is_secret_grant` matches.  A binding reaches Internal
// only by explicit `as_internal` — there is no strict-default-form
// to recognise (the strict-default-Security path resolves to
// Classified, which is captured by is_secret_grant via the
// accept_default_strict_for<Security> specialization above).
template <typename G> struct is_internal_grant
    : std::false_type {};
template <> struct is_internal_grant<grant::as_internal>
    : std::true_type {};

// `is_bg_effect_grant<G>` — true iff G is `grant::with<...>` and its
// effect pack contains `Effect::Bg`.  Used by the
// `classified_bg_without_declassify` corpus entry to detect a secret
// value flowing into a background-thread context without
// declassification.
template <typename G> struct is_bg_effect_grant
    : std::false_type {};
template <effects::Effect... Es>
struct is_bg_effect_grant<grant::with<Es...>>
    : std::bool_constant<((Es == effects::Effect::Bg) || ...)> {};

// `is_stale_grant<G>` — true iff G is a `grant::stale_to<TauMax>`
// instantiation (Staleness ≠ Fresh).  Used by the
// `staleness_secret_without_declassify` corpus entry to detect a
// classified value reachable through a stale-cache replay channel
// without a freshness-discharging declassification policy.
template <typename G> struct is_stale_grant
    : std::false_type {};
template <auto TauMax>
struct is_stale_grant<grant::stale_to<TauMax>>
    : std::true_type {};

// `is_ghost_grant<G>` — true iff G is the `grant::ghost` Usage tag
// (Usage = Ghost).  Used by the `ghost_runtime_observable` corpus
// entry to detect ghost-marked bindings that ALSO request runtime-
// observable effects (Alloc/IO/Block/Bg).  Ghost code is erased at
// compile time and MUST NOT request runtime presence.
template <typename G> struct is_ghost_grant
    : std::false_type {};
template <> struct is_ghost_grant<grant::ghost>
    : std::true_type {};

// `is_observable_effect_grant<G>` — true iff G is `grant::with<...>`
// and its effect pack contains ANY effect with a runtime observability
// footprint per the ghost-elision boundary: Alloc, IO, Block, or Bg.
//
// fixy-M-11: Init and Test are EXCLUDED from this set; the choice
// is deliberate, but the rationale was previously soft (the doc-block
// waved at "one-shot" without a concrete boundary).  Concrete reading:
//
//   - `Init` is the program-construction effect.  Ghost-code elision
//     is a TYPE-LEVEL discipline applied at TU compile time, BEFORE
//     any Init-typed body executes; a ghost binding tagged with Init
//     means "this ghost spec participates in compile-time
//     initialization computation," not "this ghost code runs at
//     startup."  Genuine ghost-runs-at-startup mistakes (ghost code
//     in a module-loader path that DOES allocate) are still caught —
//     by the Alloc/IO/Block/Bg gate, not by Init.
//
//   - `Test` is the test-context effect.  Ghost code in tests is
//     legitimate (specifications evaluated under test harnesses);
//     excluding Test admits this pattern without weakening the
//     ghost-vs-production discipline.
//
// If a future ghost-with-Init audit reveals genuine runtime-presence
// contradictions, the fix is to elevate Init into the observable
// set here AND add a new §30.14 corpus entry for the specific
// contradiction — never silently broaden the predicate.
template <typename G> struct is_observable_effect_grant
    : std::false_type {};
template <effects::Effect... Es>
struct is_observable_effect_grant<grant::with<Es...>>
    : std::bool_constant<(
        ((Es == effects::Effect::Alloc) ||
         (Es == effects::Effect::IO)    ||
         (Es == effects::Effect::Block) ||
         (Es == effects::Effect::Bg))   || ...)> {};

// fixy-M-11: structural witnesses lock the IN/OUT membership in.
// If a future contributor changes the predicate, the corresponding
// assertion fires and forces a lockstep update to the rationale
// doc-block above (and a new corpus entry where applicable).
static_assert(!is_observable_effect_grant<grant::with<effects::Effect::Init>>::value,
    "fixy-M-11: Init must remain EXCLUDED from observable effects.  "
    "Update the rationale doc-block AND add a corpus entry if "
    "the boundary changes.");
static_assert(!is_observable_effect_grant<grant::with<effects::Effect::Test>>::value,
    "fixy-M-11: Test must remain EXCLUDED from observable effects.");
static_assert( is_observable_effect_grant<grant::with<effects::Effect::Alloc>>::value,
    "fixy-M-11: Alloc must remain IN the observable set.");
static_assert( is_observable_effect_grant<grant::with<effects::Effect::IO>>::value,
    "fixy-M-11: IO must remain IN the observable set.");
static_assert( is_observable_effect_grant<grant::with<effects::Effect::Block>>::value,
    "fixy-M-11: Block must remain IN the observable set.");
static_assert( is_observable_effect_grant<grant::with<effects::Effect::Bg>>::value,
    "fixy-M-11: Bg must remain IN the observable set.");

}  // namespace detail

// ═════════════════════════════════════════════════════════════════════
// ── §30.14 Corpus — one detector per known-broken pattern ──────────
// ═════════════════════════════════════════════════════════════════════

namespace corpus {

// ── Entry 1: classified_io_without_declassify ─────────────────────
//
// Cite: §30.14 implicit-flow row.  Sabelfeld-Myers 2003, "Language-
// based information-flow security" (the IO-channel specialization;
// surveys + tightens the implicit-flow-to-IO discharge); after
// Volpano-Smith-Irvine 1996, "A sound type system for secure flow
// analysis" (the type-system foundation — VSI96 establishes the
// implicit-flow tracking framework but does not specialise to IO
// channels.  fixy-M-16 softened from leading-VSI96 attribution).
//
// Pattern: a binding engages `as_secret` (or `as_classified`) on
// Security AND `with<..., IO, ...>` on Effect AND omits any
// `declassify<Policy>` grant.  This is the canonical implicit-flow
// channel: a classified value flows out of the program through I/O
// without an audit-trail-discharging declassification.
//
// Remediation: the user must EITHER (a) project Security to a less
// restrictive level (as_public / as_unclassified), (b) drop the IO
// effect, OR (c) interpose `declassify<Policy>` with a named policy
// authorizing the flow.  The substrate's SecretConsumer stance is
// the canonical (c) form.

struct classified_io_without_declassify {
    template <typename Type, typename... Grants>
    [[nodiscard]] static consteval bool matches() noexcept {
        const bool has_secret =
            detail::has_grant_of<detail::is_secret_grant, Grants...>();
        const bool has_io =
            detail::has_grant_of<detail::is_io_effect_grant, Grants...>();
        const bool has_declassify =
            detail::has_grant_of<detail::is_declassify_grant, Grants...>();
        return has_secret && has_io && !has_declassify;
    }

    static constexpr std::string_view name() noexcept {
        return "classified_io_without_declassify";
    }

    static constexpr std::string_view cite() noexcept {
        return "Sabelfeld-Myers 2003 (after Volpano-Smith-Irvine 1996 "
               "type-system foundation) — implicit information flow: "
               "classified value flows out of the program via I/O "
               "without a declassification policy.  Insert "
               "grant::declassify<Policy> with a named policy OR drop "
               "the IO effect.";
    }

    // fixy-A4-029: per-corpus-entry pre-baked diagnostic.  The
    // assembled "entry: <name>.  <cite>" surface used by
    // `corpus_full_diagnostic_v` is computed ONCE per corpus entry at
    // first call (the static-constexpr-local stores the result for
    // every subsequent call) rather than ONCE per <Type, Grants...>
    // instantiation that hits the rejection path.  Cost reduction:
    // O(N_instantiations · C_assembly) →
    // O(6 · C_assembly + N_instantiations · C_dispatch).  The single
    // `std::string` allocation in the lambda is transient (released
    // at end of constant evaluation per P2670/P0784 dynamic-alloc
    // rules) and the assembled bytes are promoted to immutable static
    // storage via P3491R3 `std::define_static_string`.  Note: a
    // class-scope `static constexpr` data-member initializer cannot
    // call `name()` / `cite()` on the still-incomplete class — the
    // local-static form (P2647R1) defers the lambda until after the
    // class is complete, sidestepping the issue.
    static constexpr std::string_view full_diagnostic() noexcept {
        static constexpr std::string_view storage =
            []() consteval -> std::string_view {
                std::string msg;
                msg += "fixy::fn<Type, Grants...> [tier 5: "
                       "NotInTheoryCorpus]: binding matches §30.14 "
                       "unsoundness corpus entry: ";
                msg.append(name().data(), name().size());
                msg += ".  ";
                msg.append(cite().data(), cite().size());
                return std::define_static_string(msg);
            }();
        return storage;
    }
};

// ── Entry 2: classified_bg_without_declassify ────────────────────
//
// Cite: Smith-Volpano 1998, "Secure information flow in a multi-
// threaded imperative language" (POPL); Sabelfeld-Sands 2000,
// "Probabilistic noninterference for multi-threaded programs";
// Hedin-Sabelfeld 2012, "A perspective on information-flow control"
// (survey §4 — concurrency).
//
// Pattern: a binding engages `as_secret` (or `as_classified`) on
// Security AND `with<..., Bg, ...>` on Effect AND omits any
// `declassify<Policy>` grant.  This is the canonical concurrent
// information-flow channel: a classified value crosses into a
// background-thread context (scheduler-observable) without the
// audit-trail-discharging declassification.  Classical sequential
// IFC type systems are UNSOUND under concurrency — the spawn itself
// is a scheduler-observable event, so spawning behavior that depends
// on a classified value leaks the value through scheduling timing /
// thread-interleaving observability.
//
// Why this is distinct from entry 1 (classified_io_without_declassify):
// the IO entry catches data flowing out via I/O syscalls; this entry
// catches the dual scheduler-side channel where the existence and
// scheduling of background work itself encodes secret-dependent
// information.  A binding with `as_secret + with<IO, Bg>` hits BOTH
// entries; the short-circuiting OR fold reports the first match,
// but the audit reasoning carries through either way.
//
// Remediation: the user must EITHER (a) project Security to a less
// restrictive level (as_public / as_unclassified), (b) drop the Bg
// effect (run the work on the foreground thread where its scheduling
// IS deterministic), OR (c) interpose `declassify<Policy>` with a
// named policy authorizing the cross-thread flow.  The substrate's
// SecretConsumer stance is the canonical (c) form.

struct classified_bg_without_declassify {
    template <typename Type, typename... Grants>
    [[nodiscard]] static consteval bool matches() noexcept {
        const bool has_secret =
            detail::has_grant_of<detail::is_secret_grant, Grants...>();
        const bool has_bg =
            detail::has_grant_of<detail::is_bg_effect_grant, Grants...>();
        const bool has_declassify =
            detail::has_grant_of<detail::is_declassify_grant, Grants...>();
        return has_secret && has_bg && !has_declassify;
    }

    static constexpr std::string_view name() noexcept {
        return "classified_bg_without_declassify";
    }

    static constexpr std::string_view cite() noexcept {
        return "Smith-Volpano 1998 / Sabelfeld-Sands 2000 / "
               "Hedin-Sabelfeld 2012 — concurrent information flow: "
               "classified value crosses into a background-thread "
               "context without a declassification policy; the spawn "
               "is itself a scheduler-observable event.  Insert "
               "grant::declassify<Policy> OR drop the Bg effect OR "
               "project Security to a less restrictive level.";
    }

    // fixy-A4-029: see classified_io_without_declassify::full_diagnostic.
    static constexpr std::string_view full_diagnostic() noexcept {
        static constexpr std::string_view storage =
            []() consteval -> std::string_view {
                std::string msg;
                msg += "fixy::fn<Type, Grants...> [tier 5: "
                       "NotInTheoryCorpus]: binding matches §30.14 "
                       "unsoundness corpus entry: ";
                msg.append(name().data(), name().size());
                msg += ".  ";
                msg.append(cite().data(), cite().size());
                return std::define_static_string(msg);
            }();
        return storage;
    }
};

// ── Entry 3: staleness_secret_without_declassify ─────────────────
//
// Primary cite: Hunt-Sands 2008, "Just Forget It — The Semantics
// and Enforcement of Information Erasure" (POPL).  Formalizes
// erasure semantics — the dual axis of staleness.  A classified
// value reachable through a stale-cache replay window of duration
// TauMax is, semantically, a failed erasure policy: data the
// erasure-enforced system would require be forgotten remains
// observable across the replay window.  This is the closest
// formal account of the pattern this corpus entry catches.
//
// Supporting cite: Askarov-Hunt-Sabelfeld-Sands 2008, "Termination-
// Insensitive Noninterference Leaks More Than Just a Bit" (ESORICS).
// Formalizes the timing/replay channel as an information leak
// distinct from data-flow channels — relevant when the staleness
// window itself encodes timing-observable state.
//
// Survey cite: Sabelfeld-Sands 2009, "Declassification: dimensions
// and principles" (J. Computer Security).  Names the "when"
// dimension of declassification as the conceptual frame for
// temporal release authorization.  SS09 is a survey of approaches
// — it identifies the dimension exists but does not formalize the
// stale-replay-as-failed-erasure pattern this entry detects.  Kept
// as orientation for readers, demoted from primary attribution per
// fixy-H-17 (cite-discipline tightening).
//
// (fixy-CR-16: an earlier Andrysco-et-al 2015 attribution did not
// back the claim — that paper, "On Subnormal Floating Point and
// Abnormal Timing" IEEE S&P, is about FP-subnormal timing side
// channels and has nothing to say about staleness/freshness/replay
// information flow.  The substantive corpus entry is unchanged;
// only the supporting citation rotated to the correct paper.)
//
// Pattern: a binding engages `as_secret` (or `as_classified`) on
// Security AND `stale_to<TauMax>` on Staleness (non-Fresh) AND omits
// any `declassify<Policy>` grant.  This is the canonical stale-replay
// information-flow channel: a classified value is reachable through a
// stale-cache replay window of duration TauMax, exposing the value
// across the replay-window without a freshness-discharging policy.
//
// Why distinct from entries 1-2: those catch flow-through-effects
// (IO / Bg).  This entry catches flow-through-time: even with
// Effect=Row<> (no syscalls, no scheduling) and Usage=Linear (no
// aliasing), a non-Fresh Staleness reading admits a temporal channel
// where the same classified bytes are observable across the
// replay-window without authorization.  The CT (constant-time)
// substrate's CtCrypto stance is the production analogue — it pins
// Staleness to Fresh so a freshness-leak cannot occur.  Bindings that
// explicitly request stale_to<N> must also document declassify<Policy>
// authorizing the temporal flow.
//
// Remediation: EITHER (a) project Security to a less restrictive
// level, (b) drop the stale_to<N> grant (Staleness defaults to Fresh),
// OR (c) interpose `declassify<secret_policy::AuthorizedReplay>` —
// the freshness-discharging policy.  The substrate's CtCrypto stance
// is the canonical (b) form (pins Staleness=Fresh).
//
// fixy-A4-015 update: option (c) is NARROW.  ONLY a policy whose
// `axes_discharged_of` mask contains `DischargeAxis::Staleness`
// silences this matcher; the existing IO/Security-axis policies
// (AuditedLogging / WireSerialize / HashForCompare / LengthOnly /
// UserDisplay) do NOT.  Pre-A4-015 the matcher silenced on ANY
// declassify, leading to the Hunt-Sands axis-mismatch footgun where
// a policy intended for IO export silently authorized stale-replay.
// The discipline is now structurally aligned with Hunt-Sands erasure
// semantics: discharge MUST match the axis.

struct staleness_secret_without_declassify {
    template <typename Type, typename... Grants>
    [[nodiscard]] static consteval bool matches() noexcept {
        const bool has_secret =
            detail::has_grant_of<detail::is_secret_grant, Grants...>();
        const bool has_stale =
            detail::has_grant_of<detail::is_stale_grant, Grants...>();
        // fixy-A4-015: axis-specific discharge.  Pre-A4-015 ANY
        // declassify grant silenced this matcher — including
        // policies (AuditedLogging / WireSerialize / ...) whose
        // semantic authority lies on the IO or Security axis, NOT
        // Staleness.  Per Hunt-Sands 2008 erasure semantics the
        // discharge must MATCH the axis: only policies declaring
        // DischargeAxis::Staleness in `axes_discharged_of` silence
        // the staleness-replay reject.  `AuthorizedReplay` is
        // currently the sole such policy; future H-tasks may add
        // others (each must specialize `axes_discharged_of` to lift
        // the Staleness bit explicitly).
        const bool has_staleness_discharge =
            detail::has_declassify_for_axis<
                detail::DischargeAxis::Staleness, Grants...>();
        return has_secret && has_stale && !has_staleness_discharge;
    }

    static constexpr std::string_view name() noexcept {
        return "staleness_secret_without_declassify";
    }

    static constexpr std::string_view cite() noexcept {
        // fixy-A4-024: prose hierarchy made explicit.  Pre-A4-024 all
        // three citations read as a flat sequence of clauses ("primary"
        // / "supports" / "survey frame") with roughly equal prose
        // weight — reader could not tell at a glance which paper is
        // the load-bearing formalization.  Tightened form: PRIMARY in
        // caps as a header, supporting cite as a brief follow-on,
        // survey cite relegated to a parenthetical orientation note
        // with an explicit "NOT formalizing this pattern" disclaimer
        // (fixy-H-17 demotion rationale).  Remediation block separated
        // from the cite proper.
        return "Hunt-Sands 2008 'Just Forget It' (POPL) — PRIMARY: "
               "formalizes information-erasure semantics and shows a "
               "classified value reachable through a stale-replay "
               "window (stale_to<TauMax>) without a freshness-"
               "discharging declassification policy is semantically a "
               "FAILED erasure — data the policy would require be "
               "forgotten remains observable.  "
               "Supporting: Askarov-Hunt-Sabelfeld-Sands 2008 "
               "(ESORICS) formalizes the timing/replay channel as an "
               "information leak distinct from data-flow channels.  "
               "Supporting: Sabelfeld-Myers 2003 'Language-based "
               "information-flow security' (IEEE J. Sel. Areas) — "
               "the discharge MUST match the axis it authorizes; a "
               "policy for IO export does not discharge temporal "
               "replay (fixy-A4-015 per-axis tier).  "
               "(Orientation only: Sabelfeld-Sands 2009 "
               "'Declassification: dimensions and principles' names "
               "the 'when' dimension — survey identifying the axis, "
               "NOT formalizing this specific pattern; demoted from "
               "primary per fixy-H-17.)  "
               "Remediation: insert grant::declassify<secret_policy::"
               "AuthorizedReplay> (the only currently-shipped policy "
               "whose axes_discharged_of mask carries Staleness) OR "
               "drop the stale_to<N> grant (Staleness defaults to "
               "Fresh) OR project Security to a less restrictive "
               "level.  Note (fixy-A4-015): other declassify policies "
               "(AuditedLogging / WireSerialize / HashForCompare / "
               "LengthOnly / UserDisplay) do NOT silence this matcher "
               "— their authority lies on IO / Security axes, not "
               "Staleness.";
    }

    // fixy-A4-029: see classified_io_without_declassify::full_diagnostic.
    static constexpr std::string_view full_diagnostic() noexcept {
        static constexpr std::string_view storage =
            []() consteval -> std::string_view {
                std::string msg;
                msg += "fixy::fn<Type, Grants...> [tier 5: "
                       "NotInTheoryCorpus]: binding matches §30.14 "
                       "unsoundness corpus entry: ";
                msg.append(name().data(), name().size());
                msg += ".  ";
                msg.append(cite().data(), cite().size());
                return std::define_static_string(msg);
            }();
        return storage;
    }
};

// ── Entry 4: ghost_runtime_observable ────────────────────────────
//
// Cite: Filliâtre-Gondelman-Paskevich 2014, "The Spirit of Ghost
// Code" (CAV / FMSD) — the canonical formal statement of the
// ghost-vs-runtime discipline: ghost code is erased at compile
// time and must not influence the observable runtime behaviour of
// the concrete program.  Leino 2010, "Dafny: an automatic program
// verifier for functional correctness" — supporting reference, the
// first working verifier to enforce ghost-vs-concrete separation.
// Filliâtre 1999, "Preuve de programmes impératifs en théorie des
// types" — historical Why-tool root from which the 2014 discipline
// crystallised.  (fixy-CR-17: an earlier Müller-Schwerhoff-Summers
// 2016 "Viper" attribution was incorrect — Viper is a verification
// infrastructure that supports ghost code as a syntactic feature
// but does not formalise the discipline; FGP 2014 is the canonical
// statement and has been substituted.)
//
// Pattern: a binding engages `ghost` on Usage AND `with<E...>` on
// Effect where E contains at least one runtime-observable effect
// (Alloc, IO, Block, or Bg).  This breaks the ghost discipline at the
// type level: ghost values are erased at compile time and MUST NOT
// drive runtime presence.  A ghost-marked binding that allocates,
// performs I/O, blocks, or spawns background work is contradictory —
// the ghost annotation is a lie that compromises the type-erasure
// invariant.
//
// Why distinct from R006 (CollisionCatalog P002 marks_runtime_ghost_use):
// R006 requires an opt-in trait specialization (the call site
// specializes `marks_runtime_ghost_use<F>` to true).  This corpus
// entry detects the pattern purely from the Grants pack — no external
// trait specialization needed.  R006 catches a runtime-use call site;
// this entry catches the binding's TYPE-LEVEL effect-row request.
// Together they form a defense-in-depth: a binding rejected here
// never reaches the R006 use-site check.
//
// Note: Init and Test effects are deliberately EXCLUDED from the
// observable set.  `Init` is a one-shot construction-time effect that
// ghost initialization is allowed to coexist with; `Test` is a
// compile-time / no-runtime effect.  Only Alloc/IO/Block/Bg signal
// genuine runtime presence.
//
// Remediation: EITHER (a) drop the `ghost` Usage marker (the binding
// has runtime presence by design — Usage should be Linear/Affine/
// Copy/Borrow), OR (b) drop the runtime-observable effects (genuinely
// ghost code: no allocation, no I/O, no blocking, no spawning).  No
// third option — declassify does not apply (this is not a flow
// problem; it is a ghost-vs-runtime category error).

struct ghost_runtime_observable {
    template <typename Type, typename... Grants>
    [[nodiscard]] static consteval bool matches() noexcept {
        const bool has_ghost =
            detail::has_grant_of<detail::is_ghost_grant, Grants...>();
        const bool has_observable =
            detail::has_grant_of<detail::is_observable_effect_grant,
                                 Grants...>();
        return has_ghost && has_observable;
    }

    static constexpr std::string_view name() noexcept {
        return "ghost_runtime_observable";
    }

    static constexpr std::string_view cite() noexcept {
        return "Filliâtre-Gondelman-Paskevich 2014 'The Spirit of "
               "Ghost Code' / Leino 2010 'Dafny' — ghost-state "
               "discipline: a binding engaging Usage=Ghost AND any "
               "runtime-observable "
               "effect (Alloc / IO / Block / Bg) is contradictory — "
               "ghost values are erased at compile time and cannot "
               "drive runtime presence.  Drop the ghost Usage marker "
               "OR drop the runtime-observable effects.  Declassify "
               "does not apply (this is a ghost-vs-runtime category "
               "error, not an information-flow channel).";
    }

    // fixy-A4-029: see classified_io_without_declassify::full_diagnostic.
    static constexpr std::string_view full_diagnostic() noexcept {
        static constexpr std::string_view storage =
            []() consteval -> std::string_view {
                std::string msg;
                msg += "fixy::fn<Type, Grants...> [tier 5: "
                       "NotInTheoryCorpus]: binding matches §30.14 "
                       "unsoundness corpus entry: ";
                msg.append(name().data(), name().size());
                msg += ".  ";
                msg.append(cite().data(), cite().size());
                return std::define_static_string(msg);
            }();
        return storage;
    }
};

// ── Entry 5: internal_io_without_declassify ─────────────────────
//
// fixy-H-18: the §30.14 corpus covered classified→IO and classified→Bg
// but missed the symmetric Internal→IO channel.  Internal data
// (SecLevel::Internal = 2, organization-internal but not regulated-
// classified) flowing into an I/O sink without an audit-discharging
// declassification policy is the same Bell-LaPadula "no write down"
// discipline violation as the Classified→IO case — the consequence
// scale differs (leaks confidential business data vs. violates
// classified-data-handling regulations) but the IFC discipline is
// binary: any non-Public→Public flow requires declassify.
//
// Cite: Bell-LaPadula 1973 "Secure Computer Systems: Mathematical
// Foundations" — the original no-write-down formulation; Volpano-
// Smith-Irvine 1996 "A sound type system for secure flow analysis"
// — the type-system encoding of the discipline (its Sec lattice
// admits any number of intermediate tiers between Public and
// classified).  Sabelfeld-Myers 2003, "Language-based information-
// flow security" — the modern survey treats every non-bottom tier
// crossing as requiring declassification, not just the top.
//
// Pattern: a binding engages `as_internal` (SecLevel::Internal) on
// Security AND `with<..., IO, ...>` on Effect AND omits any
// `declassify<Policy>` grant.  Internal data is observable to org-
// internal services but NOT to public sinks; flowing to IO without
// declassify is an unaudited downgrade.
//
// Why distinct from entry 1 (classified_io_without_declassify): that
// entry catches `as_secret` / `as_classified` / the strict-default
// Security form (which resolves to Classified).  This entry catches
// the explicit `as_internal` form — a binding that reaches the
// Internal tier ONLY via explicit downgrade from the strict default
// (there is no strict-default-Internal form because the strict
// default resolves to Classified).  Together they cover every non-
// Public Security tier's IO-without-declassify pattern.
//
// Remediation: EITHER (a) project Security to Public/Unclassified if
// the data legitimately belongs at a public-observable tier, (b) drop
// the IO effect (keep the data org-internal — no public emission),
// OR (c) interpose `declassify<Policy>` with a named policy
// authorizing the org-internal→public downgrade.  Distinct from the
// classified-tier remediation: the policy names CAN be lighter-weight
// (organizational disclosure rather than regulatory declassification)
// but the audit-trail discipline is identical.
//
// Symmetric gap closed by entry 6 below (fixy-A4-008): the
// `as_internal + with<Bg>` channel — the concurrent dual of this IO
// entry, parallel to entry 2 (classified_bg) — is captured by
// `internal_bg_without_declassify`.  Together entries 5 + 6 cover
// every Internal-tier non-declassify discipline violation matching
// the 2-by-2 (Security tier {Secret, Internal} × Effect channel
// {IO, Bg}) closure.

struct internal_io_without_declassify {
    template <typename Type, typename... Grants>
    [[nodiscard]] static consteval bool matches() noexcept {
        const bool has_internal =
            detail::has_grant_of<detail::is_internal_grant, Grants...>();
        const bool has_io =
            detail::has_grant_of<detail::is_io_effect_grant, Grants...>();
        const bool has_declassify =
            detail::has_grant_of<detail::is_declassify_grant, Grants...>();
        return has_internal && has_io && !has_declassify;
    }

    static constexpr std::string_view name() noexcept {
        return "internal_io_without_declassify";
    }

    static constexpr std::string_view cite() noexcept {
        return "Bell-LaPadula 1973 / Volpano-Smith-Irvine 1996 / "
               "Sabelfeld-Myers 2003 — no-write-down for Internal "
               "tier: org-internal value flows into an I/O sink "
               "without a declassification policy.  Internal data is "
               "below the strict default (Classified) but ABOVE "
               "Public — every non-Public→Public crossing requires "
               "audit-trail discharge.  Insert grant::declassify"
               "<Policy> with a named organizational-disclosure "
               "policy OR drop the IO effect OR project Security to "
               "as_public / as_unclassified.";
    }

    // fixy-A4-029: see classified_io_without_declassify::full_diagnostic.
    static constexpr std::string_view full_diagnostic() noexcept {
        static constexpr std::string_view storage =
            []() consteval -> std::string_view {
                std::string msg;
                msg += "fixy::fn<Type, Grants...> [tier 5: "
                       "NotInTheoryCorpus]: binding matches §30.14 "
                       "unsoundness corpus entry: ";
                msg.append(name().data(), name().size());
                msg += ".  ";
                msg.append(cite().data(), cite().size());
                return std::define_static_string(msg);
            }();
        return storage;
    }
};

// ── Entry 6: internal_bg_without_declassify ─────────────────────
//
// fixy-A4-008: the concurrent dual of entry 5.  H-18 noted but
// scope-deferred the `as_internal + with<Bg>` channel; this entry
// closes the gap.  The IFC discipline is symmetric: a binding that
// reaches the Internal Security tier (SecLevel::Internal = 2)
// AND requests a Bg effect AND omits any `declassify<Policy>`
// grant emits Internal-tier data through a scheduler-observable
// channel without an audit-discharging policy.  Sequential
// information-flow type systems are UNSOUND under concurrency —
// the spawn itself is a scheduler-observable event, so spawn
// behaviour that depends on an Internal-tier value leaks the
// value through scheduling timing / thread-interleaving
// observability, irrespective of whether the same value would
// also leak through a data-flow IO channel.
//
// Cite: Bell-LaPadula 1973 "Secure Computer Systems: Mathematical
// Foundations" — original no-write-down formulation, applies to
// every non-Public tier (Bell-LaPadula's lattice admits any number
// of intermediate tiers); Smith-Volpano 1998 "Secure Information
// Flow in a Multi-threaded Imperative Language" (POPL) — formal
// account of concurrent IFC and the proof that sequential type
// systems do not suffice when scheduling is observable;
// Sabelfeld-Myers 2003 "Language-based information-flow security"
// — modern survey treating every non-bottom tier crossing as
// requiring declassification (the discipline is not specific to
// Classified/Secret).
//
// Pattern: a binding engages `as_internal` (SecLevel::Internal) on
// Security AND `with<..., Bg, ...>` on Effect AND omits any
// `declassify<Policy>` grant.  Distinct from entry 2
// (`classified_bg_without_declassify`) which catches the
// `as_secret`/`as_classified`/strict-default Security form; this
// entry catches the explicit `as_internal` form — a binding that
// reaches the Internal tier ONLY via explicit downgrade from the
// strict default (there is no strict-default-Internal form because
// the strict default resolves to Classified, captured by
// is_secret_grant via the accept_default_strict_for<Security>
// specialization).  Together entries 2 + 6 close the
// {Secret, Internal} × {Bg} closure; together with entries 1 + 5
// they close the full {Secret, Internal} × {IO, Bg} 2-by-2.
//
// Remediation: EITHER (a) project Security to Public/Unclassified
// if the data legitimately belongs at a public-observable tier,
// (b) drop the Bg effect (run the work on the foreground thread
// where its scheduling IS deterministic), OR (c) interpose
// `declassify<Policy>` with a named cross-thread-authorization
// policy.  Distinct from the classified-tier remediation: the
// policy names CAN be lighter-weight (organizational disclosure
// over Internal-vs-Public crossing rather than regulatory
// declassification over Classified-vs-Public) but the audit-trail
// discipline is identical and the structural Bg-spawn discipline
// is identical.

struct internal_bg_without_declassify {
    template <typename Type, typename... Grants>
    [[nodiscard]] static consteval bool matches() noexcept {
        const bool has_internal =
            detail::has_grant_of<detail::is_internal_grant, Grants...>();
        const bool has_bg =
            detail::has_grant_of<detail::is_bg_effect_grant, Grants...>();
        const bool has_declassify =
            detail::has_grant_of<detail::is_declassify_grant, Grants...>();
        return has_internal && has_bg && !has_declassify;
    }

    static constexpr std::string_view name() noexcept {
        return "internal_bg_without_declassify";
    }

    static constexpr std::string_view cite() noexcept {
        return "Bell-LaPadula 1973 / Smith-Volpano 1998 / "
               "Sabelfeld-Myers 2003 — concurrent no-write-down "
               "for Internal tier: org-internal value crosses into "
               "a background-thread context whose scheduling "
               "becomes Internal-tier-dependent.  Sequential IFC "
               "type systems are UNSOUND under concurrency (the "
               "spawn itself is a scheduler-observable event); "
               "Internal data is below the strict default "
               "(Classified) but ABOVE Public — every non-Public "
               "crossing through a scheduler-observable channel "
               "requires audit-trail discharge.  Insert "
               "grant::declassify<Policy> with a named cross-"
               "thread-authorization policy OR drop the Bg effect "
               "(run on the foreground thread where scheduling is "
               "deterministic) OR project Security to as_public / "
               "as_unclassified.";
    }

    // fixy-A4-029: see classified_io_without_declassify::full_diagnostic.
    static constexpr std::string_view full_diagnostic() noexcept {
        static constexpr std::string_view storage =
            []() consteval -> std::string_view {
                std::string msg;
                msg += "fixy::fn<Type, Grants...> [tier 5: "
                       "NotInTheoryCorpus]: binding matches §30.14 "
                       "unsoundness corpus entry: ";
                msg.append(name().data(), name().size());
                msg += ".  ";
                msg.append(cite().data(), cite().size());
                return std::define_static_string(msg);
            }();
        return storage;
    }
};

}  // namespace corpus

// ═════════════════════════════════════════════════════════════════════
// ── IsInUnsoundnessCorpus<Type, Grants...> — OR fold over entries ──
// ═════════════════════════════════════════════════════════════════════
//
// New corpus entries land here.  The OR fold short-circuits on the
// first match; the FIRST entry whose shape applies is the one that
// fires (and whose cite() drives the diagnostic).

template <typename Type, typename... Grants>
[[nodiscard]] consteval bool is_in_unsoundness_corpus() noexcept {
    return corpus::classified_io_without_declassify::matches<Type, Grants...>()
        || corpus::classified_bg_without_declassify::matches<Type, Grants...>()
        || corpus::staleness_secret_without_declassify::matches<Type, Grants...>()
        || corpus::ghost_runtime_observable::matches<Type, Grants...>()
        || corpus::internal_io_without_declassify::matches<Type, Grants...>()
        || corpus::internal_bg_without_declassify::matches<Type, Grants...>();
    // Future entries: || corpus::<next>::matches<Type, Grants...>()
}

template <typename Type, typename... Grants>
inline constexpr bool IsInUnsoundnessCorpus_v =
    is_in_unsoundness_corpus<Type, Grants...>();

// ═════════════════════════════════════════════════════════════════════
// ── NotInTheoryCorpus<Type, Grants...> — the gate-side concept ─────
// ═════════════════════════════════════════════════════════════════════
//
// The negation form is what IsAccepted composes with.  A binding is
// "accepted" iff every other check passes AND it is NOT in the
// corpus.  Same convention as the engagement gates: positive
// constraints, negative diagnostic name.

template <typename Type, typename... Grants>
concept NotInTheoryCorpus = !IsInUnsoundnessCorpus_v<Type, Grants...>;

// ═════════════════════════════════════════════════════════════════════
// ── corpus_cite_for_v — cite of the first-matching corpus entry ────
// ═════════════════════════════════════════════════════════════════════
//
// Returns the `cite()` text of the FIRST corpus entry whose
// `matches<Type, Grants...>()` returns true (matching the OR-fold
// short-circuit semantics of `is_in_unsoundness_corpus`).  Returns an
// empty `std::string_view` when no entry matches — the rejection
// path never reaches that case because `NotInTheoryCorpus` accepts
// the binding.
//
// Consumer:
//   fixy/Fn.h tier-5 `static_assert(fixy_h02_tier5_not_in_corpus, …)`
//   uses this via P2741R3 (user-generated static_assert messages) to
//   surface the matched paper + remediation text in the rejection
//   diagnostic.  Replaces fixy-H-13's documentation lie where
//   `cite()` was defined but never invoked — the prior tier-5
//   message said "see Theory.h's matched entry's cite() for the
//   literature reference," but no mechanism actually surfaced it.
//
// Discipline: add new corpus entries to the chain below in the SAME
// order as the OR fold in `is_in_unsoundness_corpus` (above).  Both
// short-circuit on first match; keeping the orders aligned preserves
// the "which entry fired" predictability across both surfaces.

template <typename Type, typename... Grants>
inline constexpr std::string_view corpus_cite_for_v =
    []() consteval -> std::string_view {
        if (corpus::classified_io_without_declassify
                ::matches<Type, Grants...>()) {
            return corpus::classified_io_without_declassify::cite();
        }
        if (corpus::classified_bg_without_declassify
                ::matches<Type, Grants...>()) {
            return corpus::classified_bg_without_declassify::cite();
        }
        if (corpus::staleness_secret_without_declassify
                ::matches<Type, Grants...>()) {
            return corpus::staleness_secret_without_declassify::cite();
        }
        if (corpus::ghost_runtime_observable
                ::matches<Type, Grants...>()) {
            return corpus::ghost_runtime_observable::cite();
        }
        if (corpus::internal_io_without_declassify
                ::matches<Type, Grants...>()) {
            return corpus::internal_io_without_declassify::cite();
        }
        if (corpus::internal_bg_without_declassify
                ::matches<Type, Grants...>()) {
            return corpus::internal_bg_without_declassify::cite();
        }
        return std::string_view{};
    }();

// ═════════════════════════════════════════════════════════════════════
// ── corpus_entry_name_for_v — struct name of first-matching entry ──
// ═════════════════════════════════════════════════════════════════════
//
// fixy-H-16: Theory.h's §IsAccepted-composition doc-block claims the
// rejection diagnostic "names which corpus entry matched (paper +
// year)".  After fixy-H-13 the cite() text surfaces paper + year via
// `corpus_cite_for_v`, but the corpus ENTRY identifier (the struct
// name a maintainer would grep Theory.h for) was NOT in the
// diagnostic.  This parallel variable returns the matched entry's
// `name()` — e.g. "classified_io_without_declassify" — so the next
// helper `corpus_full_diagnostic_v` can emit both halves of the
// "entry: <name> — <cite>" surface and make the doc-block literally
// true.
//
// Discipline: keep the if-chain ORDER identical to
// `is_in_unsoundness_corpus` and `corpus_cite_for_v`; the three
// variables must short-circuit on the same entry for the same Grants
// pack so the rejection diagnostic is internally consistent.

template <typename Type, typename... Grants>
inline constexpr std::string_view corpus_entry_name_for_v =
    []() consteval -> std::string_view {
        if (corpus::classified_io_without_declassify
                ::matches<Type, Grants...>()) {
            return corpus::classified_io_without_declassify::name();
        }
        if (corpus::classified_bg_without_declassify
                ::matches<Type, Grants...>()) {
            return corpus::classified_bg_without_declassify::name();
        }
        if (corpus::staleness_secret_without_declassify
                ::matches<Type, Grants...>()) {
            return corpus::staleness_secret_without_declassify::name();
        }
        if (corpus::ghost_runtime_observable
                ::matches<Type, Grants...>()) {
            return corpus::ghost_runtime_observable::name();
        }
        if (corpus::internal_io_without_declassify
                ::matches<Type, Grants...>()) {
            return corpus::internal_io_without_declassify::name();
        }
        if (corpus::internal_bg_without_declassify
                ::matches<Type, Grants...>()) {
            return corpus::internal_bg_without_declassify::name();
        }
        return std::string_view{};
    }();

// ═════════════════════════════════════════════════════════════════════
// ── corpus_full_diagnostic_v — combined name + cite for tier-5 ─────
// ═════════════════════════════════════════════════════════════════════
//
// fixy-H-16 (cont.): names the matched §30.14 corpus entry plus its
// cite() in the tier-5 rejection diagnostic.  fixy-A4-029 (this
// version): dispatches to the matched corpus entry's
// `full_diagnostic()` accessor rather than rebuilding the assembled
// "entry: <name> — <cite>" string per `<Type, Grants...>`
// instantiation.  Each entry pre-bakes its diagnostic ONCE
// (static-constexpr-local + P3491R3 `std::define_static_string`); this
// helper just picks one of N pre-baked string_views.  Cost reduction:
// O(N_instantiations · C_assembly) → O(6 · C_assembly +
// N_instantiations · C_dispatch).  Returns an empty `std::string_view`
// when no entry matches — the rejection path never reaches that case
// because `NotInTheoryCorpus` accepts the binding.
//
// Discipline: keep the if-chain ORDER identical to
// `is_in_unsoundness_corpus`, `corpus_cite_for_v`, and
// `corpus_entry_name_for_v`.  All four must short-circuit on the same
// entry for the same Grants pack so the rejection diagnostic is
// internally consistent.

template <typename Type, typename... Grants>
inline constexpr std::string_view corpus_full_diagnostic_v =
    []() consteval -> std::string_view {
        if (corpus::classified_io_without_declassify
                ::matches<Type, Grants...>()) {
            return corpus::classified_io_without_declassify
                ::full_diagnostic();
        }
        if (corpus::classified_bg_without_declassify
                ::matches<Type, Grants...>()) {
            return corpus::classified_bg_without_declassify
                ::full_diagnostic();
        }
        if (corpus::staleness_secret_without_declassify
                ::matches<Type, Grants...>()) {
            return corpus::staleness_secret_without_declassify
                ::full_diagnostic();
        }
        if (corpus::ghost_runtime_observable
                ::matches<Type, Grants...>()) {
            return corpus::ghost_runtime_observable::full_diagnostic();
        }
        if (corpus::internal_io_without_declassify
                ::matches<Type, Grants...>()) {
            return corpus::internal_io_without_declassify
                ::full_diagnostic();
        }
        if (corpus::internal_bg_without_declassify
                ::matches<Type, Grants...>()) {
            return corpus::internal_bg_without_declassify
                ::full_diagnostic();
        }
        // No corpus match — tier-5 succeeds; message unused.
        return std::string_view{};
    }();

}  // namespace crucible::fixy::theory
