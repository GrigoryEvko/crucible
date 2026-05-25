#pragma once

// ── crucible::fixy — Theory.h — §30.14 known-unsoundness corpus ────
//
// Per misc/16_05_2026_fixy.md §4.  This is the LOAD-BEARING
// reject-by-default surface that closes
// the FX §30.14 type-theory unsoundness corpus.  Currently ships
// TEN entries — classified_io_without_declassify,
// classified_bg_without_declassify, staleness_secret_without_declassify,
// ghost_runtime_observable, internal_io_without_declassify,
// internal_bg_without_declassify, (FOUND-019)
// external_to_verified_without_attest, (FOUND-020)
// secret_unbounded_termination_channel, (FOUND-022)
// secret_payload_without_security_claim, and (FOUND-024)
// secret_catastrophic_staleness — each pairs:
//
//   (a) a named pattern detector — a constexpr predicate over
//       (Type, Grants...) that returns true iff the binding matches
//       a known-broken shape from the literature;
//
//   (b) a doc-comment citation (paper, year, mechanism) explaining
//       WHY the pattern is unsound + which substrate primitive
//       remediates it.
//
// The corpus is DATA, but every new entry has FOUR lockstep
// touch-points in this header (not just the OR fold).  See the
// "Discipline" block below — the entry's struct is the cheap part;
// the maintenance cost is keeping all four diagnostic chains aligned
// so the rejection surface stays internally consistent.
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
//   1. A `corpus::<paper>_<year>` struct exposing four accessors —
//      `matches<Type, Grants...>()` (the constexpr detector),
//      `cite()` (paper + year + mechanism + remediation),
//      `name()` (the struct's grep-able identifier as a literal),
//      `full_diagnostic()` (the pre-baked "entry: <name> — <cite>"
//      string surfaced by tier-5 static_assert).
//   2. A doc-comment explaining the unsoundness + remediation.
//   3. A `theory_neg/` neg-compile fixture exercising it.
//   4. Four lockstep edits in THIS header, in this exact order:
//        a. `is_in_unsoundness_corpus()` — add `||
//           corpus::<entry>::matches<Type, Grants...>()` to the
//           closed-set OR fold.
//        b. `corpus_cite_for_v` — add a parallel if-clause returning
//           `<entry>::cite()` at the SAME position as (a).
//        c. `corpus_entry_name_for_v` — add a parallel if-clause
//           returning `<entry>::name()` at the SAME position.
//        d. `corpus_full_diagnostic_v` — add a parallel if-clause
//           returning `<entry>::full_diagnostic()` at the SAME
//           position.
//      All four chains short-circuit on first match.  Misalignment
//      between any of them means the rejection surface fires
//      (binding rejected by NotInTheoryCorpus) but surfaces a stale
//      / empty diagnostic — the contributor's job is to make the
//      rejection diagnostic literally true for the new entry.
//   5. Bump `corpus_size_v` AND append a new `entry_witness{...::
//      name(), ...::cite(), ...::full_diagnostic()}` row to
//      `kRoster` (see the sentinel doc-block alongside
//      `corpus_size_v` for the full deduction story).  BOTH the
//      bump and the roster append are required — the std::array
//      CTAD sentinel breaks the build on either omission, but the
//      discipline block above is the AUTHORITATIVE 5-step shape
//      that should make the sentinel firing unnecessary in
//      practice.
//
// The corpus grows monotonically by DEFAULT.  Retirement (when a
// substrate-level fix makes a §30.14 pattern impossible to express
// at the type level) is the REVERSE of the 5-step PR shape above:
// remove the entry from the OR fold + the three if-chains + the
// kRoster + decrement `corpus_size_v`.  The struct itself may be
// kept as a historical record — either commented-out next to its
// origin position, or moved into a `corpus::retired::` sub-namespace
// — with a cite to the substrate fix that obsoleted it.  Either
// form preserves audit trail without keeping the predicate active.
//
// `[[deprecated]]` on `matches<...>()` is INSUFFICIENT as a
// retirement mechanism (fixy-L-12).  Three reasons:
//
//   (a) `-Wdeprecated-declarations` fires at every OR-fold call
//       site — once per `<Type, Grants...>` instantiation that
//       reaches Theory.h — flooding the build with warnings
//       unrelated to whether THIS binding matches THIS retired
//       entry.
//
//   (b) The OR fold still EVALUATES `matches<>()` regardless of
//       deprecation — the binding still gets rejected by
//       NotInTheoryCorpus, contradicting "retired."
//
//   (c) GCC's deprecation message is a fixed string per attribute
//       site; it cannot consult `<Type, Grants...>` to explain why
//       the OLD retirement no longer applies to the CURRENT
//       binding.
//
// The OR-fold / if-chain / roster removal IS the retirement.
// `[[deprecated]]` is at most a documentation hint while a
// transitional period is active.

#include <crucible/fixy/Default.h>
#include <crucible/fixy/Dim.h>
#include <crucible/fixy/Grant.h>
#include <crucible/safety/Fn.h>
#include <crucible/safety/Secret.h>          // fixy-A4-015: AuthorizedReplay et al.

#include <array>                             // fixy-L-09/L-10: std::array CTAD for kRoster drift sentinel
#include <cstddef>
#include <cstdint>                           // fixy-A4-015: std::uint32_t for DischargeAxis
#include <meta>
#include <string>
#include <string_view>
#include <tuple>                             // FIXY-FOUND-136: secret_policy_roster
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

// ── FIXY-FOUND-005: is_secret_carrier — carrier-only Security ─────
//
// Distinct from `is_secret_grant` above (which includes
// `declassify<Policy>` per fixy-A4-015), `is_secret_carrier` is true
// ONLY for grants that *carry* classified data:
//   - grant::as_secret
//   - grant::as_classified
//   - grant::accept_default_strict_for<DimensionAxis::Security>
// It is INTENTIONALLY false for `grant::declassify<Policy>`: declassify
// is the DISCHARGE-side surface, not the carrier-side.  Entry 1's
// predicate read `is_secret_grant && !is_declassify_grant`, which for a
// `declassify<P> + IO` binding (no as_secret) self-cancelled — the
// same declassify simultaneously fired has_secret (via the A4-015
// specialization) and blocked the !has_declassify arm.  The predicate
// structurally lied that the binding contained classified-flow data.
// Switching Entry 1 to read `is_secret_carrier` makes the predicate
// honest: has_secret_carrier is true ONLY when a carrier grant is
// present, distinct from any declassify on the same binding.  Other
// corpus entries (2, 5, 6) retain `is_secret_grant` until FOUND-006's
// sweep migrates them.
template <typename G> struct is_secret_carrier
    : std::false_type {};
template <> struct is_secret_carrier<grant::as_secret>
    : std::true_type {};
template <> struct is_secret_carrier<grant::as_classified>
    : std::true_type {};
template <>
struct is_secret_carrier<grant::accept_default_strict_for<
        dim::DimensionAxis::Security>>
    : std::true_type {};
// Note: NO specialization for grant::declassify<Policy> — declassify
// is the discharge-side surface (FIXY-FOUND-005 self-cancellation fix).

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
    None        = 0u,
    Staleness   = 1u << 0,
    IO          = 1u << 1,  // reserved — see fixy-A4-015 Theory.h
    Bg          = 1u << 2,  // reserved
    Crash       = 1u << 3,  // reserved
    Reentrancy  = 1u << 4,  // reserved
    // FOUND-020: Termination — reserved bit for a future
    // secret_policy::AuthorizedDivergence (or equivalent) that admits
    // Secret-dependent unbounded-Complexity flow.  No currently-
    // shipped policy lifts this bit; entry 8 (secret_unbounded_
    // termination_channel) therefore has no current discharge path
    // and remediation reduces to "drop cost_unbounded OR drop the
    // Secret engagement" until such a policy ships.  Held to the
    // Hunt-Sands safe-default: silence requires axis-matched authority.
    Termination = 1u << 5,  // reserved — FOUND-020 / Askarov-Hunt 2008
    // FOUND-024: CatastrophicReplay — reserved bit for a future
    // secret_policy::AuthorizedCatastrophicReplay (or equivalent)
    // that admits Secret + stale_to<N> for catastrophically large
    // N.  AuthorizedReplay discharges Staleness only (any N), but a
    // catastrophic window (N >= kStaleToCatastrophic, currently
    // 1024 — over a thousand generations of replay) carries
    // qualitatively different risk than a small window and is held
    // to a stricter discharge policy.  Entry 10 (secret_catastrophic
    // _staleness) fires on Secret + stale_to<N>(N >= threshold) +
    // !CatastrophicReplay-discharge.  No policy ships this bit yet;
    // remediation reduces to "lower the stale_to N below the
    // catastrophic threshold" or "drop Secret engagement" until a
    // policy lifts the bit.  Gradient-soft staleness policy per
    // FOUND-024 premise.
    CatastrophicReplay = 1u << 6,  // reserved — FOUND-024 staleness gradient
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

// ═══════════════════════════════════════════════════════════════════
// FIXY-FOUND-136 — secret_policy roster + per-policy sentinels
// ═══════════════════════════════════════════════════════════════════
//
// Pre-FOUND-136 state: the substrate ships 6 secret_policy::* tags
// but only 2 (AuthorizedReplay → Staleness, AuditedLogging → None)
// had explicit axes_discharged_of_v sentinels above.  The other 4
// (WireSerialize, HashForCompare, LengthOnly, UserDisplay) inherited
// the safe-default None implicitly — meaning a malicious or careless
// future specialization (e.g. `axes_discharged_of<UserDisplay> =
// Staleness`) would compile clean and silently silence the §30.14
// corpus's staleness_secret_without_declassify reject.
//
// FOUND-136 closes this with three locks:
//   (a) `secret_policy_roster` — a single source-of-truth tuple
//       enumerating every policy tag.  Adding a new policy requires
//       extending the roster.
//   (b) `kPolicyRosterCardinality == 6` cardinality pin — reddens
//       loudly if the substrate adds a policy without updating the
//       roster.
//   (c) Per-policy explicit sentinels for all 6 entries (the 2
//       existing + the 4 newly-pinned) AND a fold-based closed-set
//       invariant: exactly ONE policy in the roster discharges
//       anything non-None.  A future axis-lift (e.g. WireSerialize
//       → IO) requires (i) shipping the axes_discharged_of
//       specialization in this file and (ii) bumping the
//       closed-set count below.
//
// The cardinality pin + closed-set fold together make Hunt-Sands
// safe-default discipline structurally enforced rather than
// convention-only — appending a policy without specializing the
// discharge mask leaves the closed-set count at 1 (good); changing
// an existing policy's discharge silently moves the count off 1
// (bad — fires the assertion).

namespace secret_policy_roster {

// FIXY-FOUND-018 cross-tree closure (2026-05-25): alias the
// SUBSTRATE-side roster `safety::secret_policy::roster::All` rather
// than maintain a parallel copy here.  Pre-FOUND-018 this tuple was
// hand-maintained — adding a substrate-side tag without touching
// this file silently bypassed the closed-set invariant.  Now the
// tuple lives at the SUBSTRATE source (safety/Secret.h), and this
// alias makes the fixy-side roster derive automatically.  The
// kPolicyRosterCardinality assertion below now reds AT the SUBSTRATE
// tag addition (via the substrate's own count assertion firing
// first) AND fires here on cardinality drift if the substrate count
// is bumped without updating fixy's expected count — full cross-
// tree handshake.
using AllPolicies = ::crucible::safety::secret_policy::roster::All;

inline constexpr std::size_t kPolicyRosterCardinality =
    std::tuple_size_v<AllPolicies>;

static_assert(kPolicyRosterCardinality == 6,
    "FIXY-FOUND-136 cardinality pin: secret_policy roster grew "
    "beyond 6 entries.  Adding a new policy to safety/Secret.h "
    "requires (a) appending the type to AllPolicies above, (b) "
    "shipping an axes_discharged_of<NewPolicy> specialization in "
    "this file (default None per Hunt-Sands safe-default), (c) "
    "adding an explicit sentinel below witnessing the expected "
    "discharge mask, and (d) bumping this assertion to the new "
    "count.  See safety/Secret.h secret_policy:: doc-block for the "
    "rationale on per-tag NotInherited<> + final discipline.");

}  // namespace secret_policy_roster

// ── Per-policy sentinels for the 4 previously-implicit Nones ──────
//
// AuditedLogging and AuthorizedReplay already have explicit sentinels
// above (lines 340-358).  These four close the gap so EVERY entry in
// the roster has a structurally-enforced expected discharge.

static_assert(
    axes_discharged_of_v<
        ::crucible::safety::secret_policy::WireSerialize>
        == DischargeAxis::None,
    "FIXY-FOUND-136: WireSerialize is an IO-channel serialization "
    "policy, NOT an axis-discharge policy.  Lifting to IO (when the "
    "Sabelfeld-Myers 2003 implicit-flow IO axis matcher tightens) "
    "requires explicit specialization + bumping the closed-set "
    "count below.");
static_assert(
    axes_discharged_of_v<
        ::crucible::safety::secret_policy::HashForCompare>
        == DischargeAxis::None,
    "FIXY-FOUND-136: HashForCompare is a Security-relaxation policy "
    "(release-as-hash), NOT a temporal-replay or IO discharge.  "
    "Hunt-Sands safe-default applies.");
static_assert(
    axes_discharged_of_v<
        ::crucible::safety::secret_policy::LengthOnly>
        == DischargeAxis::None,
    "FIXY-FOUND-136: LengthOnly releases only size metadata; the "
    "channel-axis taxonomy treats it as Hunt-Sands None — size "
    "telemetry IS an information channel but no current matcher "
    "axis-types it.  Future tightening would mint a dedicated "
    "DischargeAxis::SizeChannel bit and lift this sentinel.");
static_assert(
    axes_discharged_of_v<
        ::crucible::safety::secret_policy::UserDisplay>
        == DischargeAxis::None,
    "FIXY-FOUND-136: UserDisplay is a UI-render policy (e.g. last-4 "
    "of a card number).  Not a temporal-replay / IO / Bg / Crash / "
    "Reentrancy discharge; Hunt-Sands safe-default applies.");

// ── Closed-set invariant: exactly one non-None policy ──────────────
//
// Folds over the roster pack via a helper-template that consumes
// std::tuple<Ps...>.  Counts how many roster entries have a non-None
// discharge mask.  The expected count is 1 — only AuthorizedReplay
// has been lifted off Hunt-Sands safe-default.  When a future task
// lifts (e.g.) WireSerialize → IO, the count rises to 2 and THIS
// assertion fires, forcing the maintainer to acknowledge the new
// axis lift here AND update the expected count.

namespace detail::secret_policy_invariants {

template <typename TupleT>
struct non_none_policy_count;

template <typename... Ps>
struct non_none_policy_count<std::tuple<Ps...>>
    : std::integral_constant<std::size_t,
          ((axes_discharged_of_v<Ps> != DischargeAxis::None
              ? std::size_t{1} : std::size_t{0}) + ... + std::size_t{0})> {};

template <typename TupleT>
inline constexpr std::size_t non_none_policy_count_v =
    non_none_policy_count<TupleT>::value;

static_assert(non_none_policy_count_v<secret_policy_roster::AllPolicies> == 1,
    "FIXY-FOUND-136 closed-set invariant: exactly one secret_policy "
    "in the roster has a non-None axes_discharged_of mask.  The "
    "single non-None entry is AuthorizedReplay → Staleness "
    "(fixy-A4-015).  Lifting another policy off Hunt-Sands "
    "safe-default requires bumping this count AND adding an "
    "explicit per-axis sentinel above documenting WHICH axis the "
    "policy is now authoritative on.");

}  // namespace detail::secret_policy_invariants

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

// ── FIXY-FOUND-040: `is_pure_effect_grant<G>` ──────────────────────
//
// True iff `G` engages the Effect axis with a NO-effect declaration.
// Audits the bindings that the auditor cares about: "this function
// has zero observable effects."  Two structurally-different grants
// produce the same effective `effects::Row<>`:
//
//   1. `grant::with<>`                                  — POSITIVE
//      claim ("I declare zero effects").  The signature reads as
//      "this binding is pure"; the empty effect pack is the audit
//      anchor for constant-time / pure-compute discipline (e.g. the
//      `CtCrypto<T>` production stance in Fn.h:1597).
//
//   2. `grant::accept_default_strict_for<DimensionAxis::Effect>`
//      — DEFERRED form ("I accept the strict default").  The strict
//      default for Effect is `effects::Row<>` (Default.h:158); a
//      binding that engages Effect via the deferred form resolves
//      to the SAME effective row as a `with<>` engagement.
//
// Without this predicate, an auditor grepping for "every pure-
// effect binding" must search both `grant::with<>` and
// `accept_default_strict_for<.*Effect>` sites — and the equivalence
// is invisible to type-system queries.  The defect mirrors the
// `is_secret_grant` design tension closed in fixy-CR-01 (line 218)
// where the strict-default-Security form was specialized into the
// existing predicate to unify positive + deferred engagement
// detection.
//
// Closure: a single audit-grep target `is_pure_effect_grant_v<G>`
// that recognises BOTH forms, plus an invariant static_assert
// pinning the premise (`strict_default_for<Effect>::type ==
// effects::Row<>`).  If anyone weakens the strict default (e.g. by
// inserting a non-empty effect row), the build breaks AND the
// predicate's semantic equivalence must be re-evaluated.

template <typename G> struct is_pure_effect_grant
    : std::false_type {};

// Positive form — empty effect pack.
template <>
struct is_pure_effect_grant<grant::with<>>
    : std::true_type {};

// Deferred form — accept_default_strict_for<Effect>.
template <>
struct is_pure_effect_grant<grant::accept_default_strict_for<
        dim::DimensionAxis::Effect>>
    : std::true_type {};

template <typename G>
inline constexpr bool is_pure_effect_grant_v = is_pure_effect_grant<G>::value;

// Invariant pin — the deferred form's semantic equivalence to the
// positive form HOLDS only while strict_default_for<Effect> resolves
// to effects::Row<>.  A future audit that injects a non-empty default
// row would silently re-route the deferred form to a non-pure
// engagement; the build breaks here before that can happen.
static_assert(
    std::is_same_v<typename strict_default_for<dim::DimensionAxis::Effect>::type,
                   effects::Row<>>,
    "FIXY-FOUND-040 invariant: strict_default_for<Effect> must "
    "resolve to effects::Row<>.  The deferred-form specialization "
    "of is_pure_effect_grant relies on this equivalence — any "
    "change here requires re-evaluating the predicate's semantic "
    "correctness.");

// ── FIXY-FOUND-040 — predicate coverage matrix ─────────────────────
//
// 8-row witness pinning the predicate's discrimination boundary.
// Each assertion documents WHY the cell evaluates as it does — a
// future audit reading any single row should understand the
// semantic basis without consulting the full predicate definition.
namespace detail::found_040_witness {

// (1) Positive form — `with<>` empty effect pack: TRUE.
static_assert(is_pure_effect_grant_v<grant::with<>>,
    "FIXY-FOUND-040: grant::with<> (positive empty-pack form) MUST "
    "satisfy is_pure_effect_grant — it IS the audit anchor for "
    "pure-compute bindings.");

// (2) Deferred form — accept_default_strict_for<Effect>: TRUE.
static_assert(is_pure_effect_grant_v<grant::accept_default_strict_for<
                  dim::DimensionAxis::Effect>>,
    "FIXY-FOUND-040: deferred-form accept_default_strict_for<Effect> "
    "resolves to effects::Row<> via strict_default_for<Effect>; "
    "the predicate unifies positive + deferred under one query.");

// (3) Single-effect with<IO>: FALSE.
static_assert(!is_pure_effect_grant_v<grant::with<effects::Effect::IO>>,
    "FIXY-FOUND-040: with<IO> declares a NON-EMPTY effect row — "
    "the binding IS NOT pure.");

// (4) Multi-effect with<Alloc, IO>: FALSE.
static_assert(!is_pure_effect_grant_v<grant::with<effects::Effect::Alloc,
                                                  effects::Effect::IO>>,
    "FIXY-FOUND-040: any non-empty effect pack disqualifies "
    "the binding from the pure-effect audit class.");

// (5) Convenience alias with_io: FALSE (resolves to with<IO>).
static_assert(!is_pure_effect_grant_v<grant::with_io>,
    "FIXY-FOUND-040: convenience aliases inherit non-pure status "
    "through their underlying with<E> specialization.");

// (6) Convenience alias with_bg: FALSE.
static_assert(!is_pure_effect_grant_v<grant::with_bg>,
    "FIXY-FOUND-040: with_bg engages Effect with Bg — distinct "
    "from pure-effect.  Audited separately by is_bg_effect_grant.");

// (7) Wrong-axis deferred — accept_default_strict_for<Usage>: FALSE.
static_assert(!is_pure_effect_grant_v<grant::accept_default_strict_for<
                   dim::DimensionAxis::Usage>>,
    "FIXY-FOUND-040: the deferred form is axis-keyed — only the "
    "Effect-axis deferred grant matches.  Other axes' deferred "
    "forms (Usage, Security, etc.) MUST NOT cross-match.");

// (8) Non-grant types — FALSE (catch-all primary template).
static_assert(!is_pure_effect_grant_v<int>,
    "FIXY-FOUND-040: non-grant types fall through to the primary "
    "false_type template.  The predicate is total over the type "
    "universe.");
static_assert(!is_pure_effect_grant_v<grant::as_secret>,
    "FIXY-FOUND-040: Security-axis grants do not match the "
    "Effect-axis pure-effect predicate, even when sharing the "
    "is_*_grant naming convention.");

}  // namespace detail::found_040_witness

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

// ── FIXY-FOUND-024: magnitude-aware stale_to predicate ─────────────
//
// `is_stale_grant<G>` is a binary detector — it answers "is G a
// stale_to<N> for any N?" but the magnitude of N is structurally
// invisible.  Entry 4 (staleness_secret_without_declassify) treats
// stale_to<5> and stale_to<10000> as semantically equivalent: both
// fire the matcher, both silence on AuthorizedReplay.  This opacity
// hides the qualitative difference between "the value is one
// generation old" (mild replay surface) and "the value is ten
// thousand generations old" (catastrophic replay surface).
//
// The closure: an NTTP-capturing extractor `stale_to_value<G>` that
// surfaces TauMax at the type level, and a fold helper
// `any_stale_to_at_least<Threshold, Grants...>()` that filters by
// magnitude.  Entry 10 (secret_catastrophic_staleness) uses these
// to add a second-tier reject for catastrophic windows that
// requires a stricter discharge policy (DischargeAxis::Catastrophic
// Replay) than AuthorizedReplay carries.
//
// `stale_to_value<G>::value` — uint64_t-canonicalized TauMax if G
// is `grant::stale_to<N>`, otherwise 0 (the sentinel for "not a
// stale_to grant").  uint64_t canonicalization handles the
// heterogeneous-NTTP-type case (stale_to<5> uses int 5, stale_to
// <5ULL> uses unsigned long long) so the threshold predicate
// compares apples-to-apples.

template <typename G>
struct stale_to_value {
    static constexpr std::uint64_t value = 0;
};
template <auto N>
struct stale_to_value<grant::stale_to<N>> {
    static constexpr std::uint64_t value = static_cast<std::uint64_t>(N);
};

// `any_stale_to_at_least<Threshold, Grants...>()` — fold-OR over
// the grant pack, returns true iff any G in Grants is a
// `grant::stale_to<N>` whose N (canonicalized to uint64_t) is at
// least `Threshold`.  Empty pack returns false (no stale claim →
// no catastrophic claim).
template <std::uint64_t Threshold, typename... Grants>
[[nodiscard]] consteval bool
any_stale_to_at_least() noexcept {
    if constexpr (sizeof...(Grants) == 0) {
        return false;
    } else {
        return ((stale_to_value<Grants>::value >= Threshold) || ...);
    }
}

// FOUND-024 threshold constant.  1024 generations is the
// conservative default for "catastrophic" replay window — over a
// thousand generations of stale state carries qualitatively
// different risk (large enough to amortize replay-channel attacks
// across many observations) than smaller windows.  Future tuning
// per workload should override via a fixy-config mechanism (not
// shipped); current default is a single source-of-truth audit
// anchor.
inline constexpr std::uint64_t kStaleToCatastrophic = 1024;

// Structural witnesses — pin the canonical positive/negative cases
// so a refactor of stale_to or the NTTP extractor reds these BEFORE
// entry 10 silently misfires.
static_assert(stale_to_value<grant::stale_to<5>>::value == 5u,
    "FIXY-FOUND-024: stale_to<5> must surface TauMax = 5 via the "
    "uint64_t canonicalization.  If this reds, grant::stale_to's "
    "template shape changed (e.g. dropped the auto NTTP) and the "
    "extractor specialization needs to follow.");
static_assert(stale_to_value<grant::stale_to<10000>>::value == 10000u,
    "FIXY-FOUND-024: stale_to<10000> must surface TauMax = 10000 "
    "(canonical large-window witness for entry 10).");
static_assert(stale_to_value<grant::as_secret>::value == 0u,
    "FIXY-FOUND-024: non-stale_to grants must surface 0 (the "
    "sentinel for 'no stale claim') — Security-axis grants must "
    "NOT leak into the staleness-magnitude predicate.");
static_assert(any_stale_to_at_least<kStaleToCatastrophic,
              grant::stale_to<5000>>(),
    "FIXY-FOUND-024: stale_to<5000> exceeds the catastrophic "
    "threshold (1024) and must trigger entry 10's magnitude-aware "
    "reject.");
static_assert(!any_stale_to_at_least<kStaleToCatastrophic,
              grant::stale_to<100>>(),
    "FIXY-FOUND-024: stale_to<100> is below the catastrophic "
    "threshold (1024) and must NOT trigger entry 10 — small "
    "windows route through entry 4 with normal AuthorizedReplay "
    "discharge.");
static_assert(!any_stale_to_at_least<kStaleToCatastrophic>(),
    "FIXY-FOUND-024: empty grant pack must return false from the "
    "magnitude predicate (no stale claim implies no catastrophic "
    "claim).");

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
// FIXY-FOUND-133: route through effects::is_observable<E> (Pattern B
// reflection-driven gate in effects/Capabilities.h).  Prior body
// hardcoded the IN-set `(Alloc || IO || Block || Bg)`; a new Effect
// atom (Crash / Network) would have silently defaulted to "not
// observable", which is the FIXY-FOUND-017 forward-compat cliff.
// The atom-helper's switch has no default branch → -Werror=switch
// reddens the build on a new atom that isn't deliberately classified.
template <effects::Effect... Es>
struct is_observable_effect_grant<grant::with<Es...>>
    : std::bool_constant<(effects::is_observable<Es>() || ...)> {};

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

// ── FIXY-FOUND-019 detectors (Biba/Clark-Wilson integrity dual) ──
//
// The §30.14 corpus modeled the Bell-LaPadula CONFIDENTIALITY axis
// (no-write-down, entries 1/2/5/6) but lacked the INTEGRITY dual
// (Biba 1977 no-write-up, Clark-Wilson 1987 Transformation
// Procedure).  Entry 7 (external_to_verified_without_attest) closes
// that gap.  Detectors below identify the three grant shapes the
// integrity matcher inspects.

// `is_external_source_grant<G>` — true iff G is `grant::from_source<
// safety::source::External>`, the canonical low-integrity provenance
// tag for raw untrusted input (network bytes, FFI handoffs, raw user
// text).  Tagged.h:75 declares the source tag; retag_policy<External,
// IntegrityVerified> is the canonical discharge path (Tagged.h:710).
template <typename G> struct is_external_source_grant
    : std::false_type {};
template <>
struct is_external_source_grant<
    grant::from_source<::crucible::safety::source::External>>
    : std::true_type {};

// `is_trust_verified_grant<G>` — true iff G is `grant::trust_verified`,
// the high-integrity Trust-axis claim (Grant.h:510).  A binding that
// engages this is asserting "my output is verified-trustworthy" —
// the Biba/Clark-Wilson "Constrained Data Item" producer position.
template <typename G> struct is_trust_verified_grant
    : std::false_type {};
template <>
struct is_trust_verified_grant<grant::trust_verified>
    : std::true_type {};

// `is_trust_assumed_grant<G>` — true iff G is `grant::trust_assumed<
// Rationale>` (any Rationale NTTP per FOUND-038's axis_query_tag
// discipline).  The grant is Crucible's Clark-Wilson "Transformation
// Procedure": developer-documented integrity attestation bridging
// External → Verified with an audit-grade rationale literal.
template <typename G> struct is_trust_assumed_grant
    : std::false_type {};
template <auto Rationale>
struct is_trust_assumed_grant<grant::trust_assumed<Rationale>>
    : std::true_type {};

// Structural witnesses — pin the canonical positive/negative cases so
// a future refactor that changes from_source / trust_verified /
// trust_assumed shape reds these BEFORE the corpus entry below
// silently misfires.
static_assert(is_external_source_grant<
    grant::from_source<::crucible::safety::source::External>>::value,
    "FIXY-FOUND-019: from_source<External> must remain detectable as "
    "the low-integrity provenance tag.  If this reds, source::External "
    "was renamed or from_source's shape changed.");
static_assert(!is_external_source_grant<
    grant::from_source<::crucible::safety::source::Sanitized>>::value,
    "FIXY-FOUND-019: from_source<Sanitized> must NOT match the "
    "External detector — Sanitized is the DISCHARGED tag (retag from "
    "External admitted at Tagged.h:706).");
static_assert(is_trust_verified_grant<grant::trust_verified>::value,
    "FIXY-FOUND-019: trust_verified must remain detectable as the "
    "high-integrity Trust claim.");
static_assert(is_trust_assumed_grant<
    grant::trust_assumed<grant::axis_query_tag>>::value,
    "FIXY-FOUND-019: trust_assumed<Rationale> with any Rationale NTTP "
    "must match the attestation detector.  If this reds, FOUND-038's "
    "mandatory-Rationale form was broken.");

// ── FIXY-FOUND-020 detector (Askarov-Hunt termination channel) ────
//
// Askarov-Hunt-Sabelfeld-Sands 2008 ESORICS "Termination-Insensitive
// Noninterference Leaks More Than Just a Bit" formalizes that even
// termination-insensitive noninterference admits unbounded
// information leak via observable termination behavior.  Pattern:
// a binding engages Secret AND admits unbounded execution on the
// Complexity axis (Grant.h:551 grant::cost_unbounded → DimensionAxis
// ::Complexity) WITHOUT a declassify<Policy> whose
// `axes_discharged_of` covers `DischargeAxis::Termination`.  An
// observer can then encode arbitrary bits of the Secret into the
// program's termination decisions, exfiltrating the secret over
// repeated observations.  No currently-shipped declassify policy
// lifts the Termination bit — remediation reduces to "drop
// cost_unbounded (claim bounded complexity) OR drop the Secret
// engagement (project to as_public / as_unclassified)" per
// Hunt-Sands safe-default discipline.
//
// `is_cost_unbounded_grant<G>` — true iff G is `grant::cost_unbounded`,
// the Complexity-axis claim admitting unbounded execution (loops,
// unbounded recursion, divergent paths).  Distinct from
// `cost_constant` which pins Complexity to O(1) and from absence-
// of-grant which defaults to a more conservative bound per the
// axis's strict-default policy.
template <typename G> struct is_cost_unbounded_grant
    : std::false_type {};
template <>
struct is_cost_unbounded_grant<grant::cost_unbounded>
    : std::true_type {};

// Structural witnesses — pin the canonical positive/negative cases
// so a future refactor that changes cost_unbounded shape OR
// introduces a divergence-discharging policy reds these BEFORE
// the corpus entry below silently misfires.
static_assert(is_cost_unbounded_grant<grant::cost_unbounded>::value,
    "FIXY-FOUND-020: grant::cost_unbounded must remain detectable as "
    "the Complexity-axis unbounded-execution claim (Askarov-Hunt-"
    "Sabelfeld-Sands 2008 termination-channel pattern).  If this "
    "reds, the grant tag was renamed or relocated.");
static_assert(!is_cost_unbounded_grant<grant::cost_constant>::value,
    "FIXY-FOUND-020: cost_constant must NOT match the unbounded "
    "detector — cost_constant pins Complexity to O(1), the safe-"
    "default case that does NOT admit a termination channel.");
static_assert(!is_cost_unbounded_grant<grant::as_secret>::value,
    "FIXY-FOUND-020: Security-axis grants must NOT match the "
    "Complexity-axis cost_unbounded detector (cross-axis discipline).");

// ═════════════════════════════════════════════════════════════════════
// ── FIXY-FOUND-042 — ImplicitTypeMarker inertness roster ──────────
// ═════════════════════════════════════════════════════════════════════
//
// The wrapper-discipline `IsAccepted<T, Grants...>` (Reject.h §1493)
// auto-injects `detail::accept::ImplicitTypeMarker` (=
// `grant::accept_default_strict_for<DimensionAxis::Type>`) into the
// Grants pack before delegating to `IsAcceptedDirect`.  The §30.14
// corpus matchers consume the SAME pack via `has_grant_of<Predicate,
// Grants...>()` — OR-folding per-grant predicates over EVERY pack
// member, including the injected marker.
//
// DEFECT (latent): if any `is_*_grant<G>` predicate erroneously
// matches `accept_default_strict_for<Type>` — most plausibly via a
// permissive `accept_default_strict_for<auto>` specialization that
// doesn't axis-discriminate — the matcher counts the injected
// marker as a positive PAYLOAD CLAIM the developer never made.  The
// binding would silently fire a §30.14 unsoundness diagnostic
// (false-positive corpus match) OR silently SUPPRESS a real
// diagnostic (e.g., a permissive `is_declassify_grant` match would
// short-circuit the corpus's "no-declassify" gate).
//
// Two existing axis-keyed deferred-form specializations exist
// today: `is_secret_grant<accept_default_strict_for<Security>>`
// (fixy-CR-01) and `is_pure_effect_grant<accept_default_strict_for
// <Effect>>` (FOUND-040).  Both axis-discriminate via the
// DimensionAxis NTTP — neither matches the Type-axis injection.
//
// Closure: a fold-over-roster audit that asserts EVERY per-grant
// predicate returns `false` on the canonical ImplicitTypeMarker
// type.  Adding a new `is_*_grant` predicate REQUIRES appending a
// row to the roster AND bumping the cardinality pin — both
// changes enforced at the definition site, not at a distant
// consumer.

namespace detail::found_042_witness {

// Canonical ImplicitTypeMarker structural form — Reject.h:1431.
// Pinned by FOUND-041 (is_same_v witness) to BE the substrate's
// `detail::accept::ImplicitTypeMarker`; here we reference the type
// directly without including Reject.h (which Theory.h ALREADY
// imports transitively, but the literal form makes the roster
// self-contained for review).
using TypeMarker =
    grant::accept_default_strict_for<dim::DimensionAxis::Type>;

// PredicateRef<Predicate> — wraps a per-grant predicate template so
// it can be stored in a tuple.  `inert_on_marker_v` evaluates the
// predicate ON the marker type and reports `true` iff the predicate
// is INERT (returns false_type).
template <template <typename> class Predicate>
struct PredicateRef {
    static constexpr bool inert_on_marker_v =
        !Predicate<TypeMarker>::value;
};

// Roster of every per-grant predicate that corpus matchers fold
// through `has_grant_of`.  Order is the definition order above
// (line-number-sorted).
using AllCorpusPredicates = std::tuple<
    PredicateRef<is_secret_grant>,
    PredicateRef<is_declassify_grant>,
    PredicateRef<is_io_effect_grant>,
    PredicateRef<is_internal_grant>,
    PredicateRef<is_bg_effect_grant>,
    PredicateRef<is_pure_effect_grant>,         // FOUND-040
    PredicateRef<is_stale_grant>,
    PredicateRef<is_ghost_grant>,
    PredicateRef<is_observable_effect_grant>,
    PredicateRef<is_external_source_grant>,
    PredicateRef<is_trust_verified_grant>,
    PredicateRef<is_trust_assumed_grant>,
    PredicateRef<is_cost_unbounded_grant>>;

// Fold helper — AND-reduce `inert_on_marker_v` over the roster.
template <typename Tuple>
[[nodiscard]] consteval bool all_inert_on_marker() noexcept {
    return [&]<std::size_t... Is>(std::index_sequence<Is...>) consteval {
        return (std::tuple_element_t<Is, Tuple>::inert_on_marker_v && ...);
    }(std::make_index_sequence<std::tuple_size_v<Tuple>>{});
}

// (1) Load-bearing roster assertion — every audited predicate is
// inert on ImplicitTypeMarker.  Fires if a future predicate is
// added to the roster AND that predicate erroneously matches the
// marker.
static_assert(all_inert_on_marker<AllCorpusPredicates>(),
    "FIXY-FOUND-042: every per-grant corpus predicate MUST return "
    "false_type on accept_default_strict_for<DimensionAxis::Type> "
    "(the canonical ImplicitTypeMarker).  A predicate that matches "
    "the marker would treat the wrapper's auto-injection as a "
    "user-supplied payload claim, polluting §30.14 corpus "
    "matchers.  Axis-keyed deferred-form specializations (e.g., "
    "is_secret_grant<accept_default_strict_for<Security>>) MUST "
    "axis-discriminate via the DimensionAxis NTTP; a permissive "
    "accept_default_strict_for<auto> specialization is FORBIDDEN.");

// (2) Cardinality pin — the count of audited predicates is
// STRUCTURALLY pinned.  Adding a new per-grant predicate without
// extending the roster fires a build error here (the count
// asserts diverges from the comment-stated count).  Reciprocal:
// removing a predicate without bumping the count also fires.
inline constexpr std::size_t kAuditedCorpusPredicateCount =
    std::tuple_size_v<AllCorpusPredicates>;
static_assert(kAuditedCorpusPredicateCount == 13,
    "FIXY-FOUND-042 cardinality pin: 13 per-grant predicates "
    "currently audited.  Bumping requires (a) appending the new "
    "PredicateRef<is_NEW_grant> to AllCorpusPredicates AND (b) "
    "incrementing this literal.  A drift between roster and count "
    "fires here.");

// (3) Per-predicate explicit witnesses — pin each axis-keyed
// specialization individually.  These are redundant with (1) but
// surface the SPECIFIC predicate at fault when a diagnostic
// fires, rather than the opaque "all_inert_on_marker fold".
static_assert(!is_secret_grant<TypeMarker>::value,
    "FIXY-FOUND-042: is_secret_grant's accept_default_strict_for"
    "<Security> specialization MUST axis-discriminate — Type-axis "
    "marker must NOT match.");
static_assert(!is_pure_effect_grant<TypeMarker>::value,
    "FIXY-FOUND-042: is_pure_effect_grant's accept_default_strict"
    "_for<Effect> specialization MUST axis-discriminate — "
    "Type-axis marker must NOT match (FOUND-040 closure).");

// (4) Soundness chain — IF every per-grant predicate is inert on
// ImplicitTypeMarker (assertion (1) above), THEN the
// `has_grant_of<Predicate, Grants...>()` OR-fold over a Grants
// pack augmented with the marker is identical to the same fold
// over the unaugmented pack:
//
//     has_grant_of<P, G..., TypeMarker>()
//   ≡ has_grant_of<P, G...>() || P<TypeMarker>::value
//   ≡ has_grant_of<P, G...>() || false       (by assertion (1))
//   ≡ has_grant_of<P, G...>()
//
// Therefore every §30.14 corpus matcher — which is a Boolean
// combination of `has_grant_of<P_i, ...>` calls — returns the
// IDENTICAL result on a wrapper-injected vs direct-call grants
// pack.  This is the structural witness FOUND-041 required for
// the marker injection to be SEMANTICALLY transparent to corpus
// matching.  Assertion (1) IS the closure; no end-to-end runtime
// assertion is needed (and one would forward-reference corpus::
// which is defined further down in this file).

}  // namespace detail::found_042_witness

// ── FIXY-FOUND-022 detector (payload-classification opacity) ──────
//
// Pre-022 the §30.14 corpus matchers all took `<typename Type,
// typename... Grants>` but bodies only inspected `Grants...`.  Type
// was structurally ignored — a function whose payload IS `safety::
// Secret<U>` (the value type encodes secrecy at the wrapper level)
// passed every matcher unless it ALSO engaged Security via grants.
// This is a policy-data divergence: the WRAPPER says one thing
// ("the value is secret"), the type-level POLICY claims another
// (no security engagement).  Entry 9
// (secret_payload_without_security_claim) closes that opacity by
// detecting Secret-wrapped payloads at the Type slot directly.
//
// `is_secret_type<T>` — true iff T is `crucible::safety::Secret<U>`
// for any U.  Distinct from `is_secret_grant` which inspects the
// Grants pack: this detector inspects the BINDING's Type slot, the
// payload position.  Future Secret-equivalent wrappers (e.g. a
// `Classified<T>` if it ships) would extend via additional
// specializations on the same template.
template <typename T> struct is_secret_type
    : std::false_type {};
template <typename T>
struct is_secret_type<::crucible::safety::Secret<T>>
    : std::true_type {};

// Structural witnesses — pin the canonical positive/negative cases
// so a future refactor that changes Secret<T>'s template shape OR
// adds a new Secret-equivalent wrapper reds these BEFORE entry 9
// silently misfires.
static_assert(is_secret_type<::crucible::safety::Secret<int>>::value,
    "FIXY-FOUND-022: Secret<int> must match the payload-secret "
    "detector — the canonical Secret<T>-wrapped payload position.  "
    "If this reds, Secret's template shape changed (e.g., added a "
    "second template parameter) and the specialization above needs "
    "to follow.");
static_assert(!is_secret_type<int>::value,
    "FIXY-FOUND-022: bare int must NOT match the secret-payload "
    "detector — type-level secrecy requires explicit Secret<T> "
    "wrapping at the payload position.");
static_assert(!is_secret_type<::crucible::safety::Secret<int>*>::value,
    "FIXY-FOUND-022: pointer-to-Secret must NOT match — the Secret "
    "wrapping is one indirection level removed and the pointer "
    "payload itself is not classified.  If callers want pointer-to-"
    "Secret to fire entry 9, that is a separate specialization "
    "decision (covered by a future FIXY-FOUND-* task).");

// ── FIXY-FOUND-023 sentinel (SecLevel growth audit) ────────────────
//
// `is_secret_grant` (above) hardcodes specializations for a closed
// set of grant tags: `as_secret`, `as_classified`, `declassify<P>`,
// and `accept_default_strict_for<Security>`.  Each of these
// engagements corresponds to a position on the `safety::SecLevel`
// lattice (Unclassified / Public / Internal / Classified / Secret).
// If the SecLevel enum grows — e.g., a future `SecLevel::TopSecret`
// to represent a tier above Secret, or a refinement between two
// existing tiers — the corresponding `as_<tier>` grant must be
// authored AND `is_secret_grant` must be audited: if the new tier
// represents secrecy-raising (must remain confidential without an
// explicit declassify), it requires a new `is_secret_grant`
// specialization; if it represents projection-down (an additional
// "less classified" tier), no `is_secret_grant` update is needed
// (downward projection is intentionally NOT a silencer per
// FOUND-022 entry 9 cite correction), but Security-axis-aware
// detectors elsewhere in the codebase should be audited.
//
// `safety::sec_level_count()` (defined in `safety/Fn.h:712`) returns
// the live enumerator count via P2996 `enumerators_of` reflection
// over the SecLevel enum.  This sentinel pins the count at 5 so a
// build-time green build is the witness that no SecLevel-tier
// addition has slipped past the is_secret_grant audit discipline.
// Bumping `sec_level_count() == N` requires a deliberate audit
// commit that EITHER extends is_secret_grant (secrecy-raising
// tier) OR documents the tier as projection-down (no detector
// extension).  This closes the FOUND-023 "closed-set fragile for
// future SecLevel tier additions" surface structurally — adding
// a SecLevel enumerator now reds the build instead of silently
// bypassing the corpus.
static_assert(std::meta::enumerators_of(
        ^^::crucible::safety::fn::SecLevel).size() == 5,
    "FIXY-FOUND-023: safety::SecLevel grew beyond its 5-enumerator "
    "audit anchor (Unclassified/Public/Internal/Classified/Secret "
    "as of FOUND-023 ship).  `is_secret_grant` in this header "
    "hardcodes specializations for `as_secret`, `as_classified`, "
    "`declassify<P>`, and `accept_default_strict_for<Security>` — "
    "all of which engage secrecy-raising or declassification on "
    "the existing 5-tier lattice.  A new SecLevel enumerator does "
    "NOT automatically extend `is_secret_grant`; the maintainer "
    "MUST audit: (a) is the new tier secrecy-raising (above the "
    "old Secret top)?  → add an `is_secret_grant` specialization "
    "for the corresponding `as_<tier>` grant.  (b) is the new "
    "tier projection-down (between Unclassified and Public, or "
    "below Unclassified)?  → no `is_secret_grant` update needed "
    "(downward projection is not a silencer per FOUND-022 entry 9 "
    "cite).  Other Security-axis-aware detectors across the "
    "codebase (corpus matchers, retag_policy specializations, "
    "stance tables) should be re-audited regardless.  Bump this "
    "5 to the new count after auditing.");

// ── FIXY-FOUND-025: shared full-diagnostic storage ─────────────────
//
// Pre-025, every corpus entry's `full_diagnostic()` materialized a
// function-local `static constexpr std::string_view storage` from a
// consteval lambda.  Function-local statics are per-TU: every TU
// that includes Theory.h paid the consteval evaluation cost AND
// produced a separate storage slot (the linker may dedup the
// content via define_static_string, but the per-TU consteval
// re-evaluation is unavoidable for function-local statics).  For
// 10 corpus entries × N TUs that include Theory.h, compile-time
// cost was 10 × N consteval lambda evaluations.
//
// Closure: a per-entry **inline variable template** at namespace
// detail:: scope, parametrized on the entry's name() and cite()
// function-pointer NTTPs.  Inline variable templates are
// inline-by-default (C++17), so each <NameFn, CiteFn> instantiation
// has a SINGLE address across ALL TUs.  Compile-time cost drops
// from 10 × N to 10 — one consteval evaluation per entry, dedup'd
// across the entire build.  Per-entry full_diagnostic() reduces to
// a one-line return statement.
//
// The NTTP-function-pointer parametrization (`&name, &cite` from
// inside each entry's class body) keeps the per-entry text
// IDENTICAL across all 10 entries (class-scoped lookup resolves
// `name` and `cite` to the current entry's static methods), which
// lets a single replace_all migrate the entire corpus in one edit.
template <auto NameFn, auto CiteFn>
inline constexpr std::string_view kCorpusFullDiagnostic =
    []() consteval -> std::string_view {
        std::string msg;
        msg += "fixy::fn<Type, Grants...> [tier 5: "
               "NotInTheoryCorpus]: binding matches §30.14 "
               "unsoundness corpus entry: ";
        std::string_view name_sv = NameFn();
        std::string_view cite_sv = CiteFn();
        msg.append(name_sv.data(), name_sv.size());
        msg += ".  ";
        msg.append(cite_sv.data(), cite_sv.size());
        return std::define_static_string(msg);
    }();

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
        // FIXY-FOUND-005: has_secret reads `is_secret_carrier` (NOT
        // `is_secret_grant`).  Pre-fix, both arms of the predicate
        // (`has_secret` and `has_declassify`) were satisfied by the same
        // `grant::declassify<P>` (per fixy-A4-015 making declassify
        // engage the Security axis).  A declassify-only binding hit
        // has_secret=TRUE && has_declassify=TRUE → predicate FALSE →
        // admit, but the structural reason ("classified data present")
        // was a lie — the same grant simultaneously fired the carrier
        // arm AND blocked the matcher.  Carrier-only detection ensures
        // has_secret reflects an as_secret / as_classified / strict-
        // default-Security grant, distinct from any declassify on the
        // same binding.  Outcomes for canonical cases (as_secret + IO
        // + no_declassify still REJECTS; as_secret + IO + declassify
        // still admits; declassify-only + IO still admits) unchanged;
        // the predicate is now structurally honest.  Future downstream
        // consumers reading `has_secret` see what the name claims.
        const bool has_secret =
            detail::has_grant_of<detail::is_secret_carrier, Grants...>();
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
        // FOUND-025: per-TU function-local-static eliminated; storage
        // now lives in a single inline-variable-template instantiation
        // keyed on this entry's (&name, &cite) NTTP function-pointer
        // pair, dedup'd across all TUs at link time.
        return ::crucible::fixy::theory::detail::kCorpusFullDiagnostic<
            &name, &cite>;
    }
};

// ── Entry 2: classified_bg_without_declassify ────────────────────
//
// Cite: Smith-Volpano 1998, "Secure information flow in a multi-
// threaded imperative language" (POPL); Sabelfeld-Sands 2000,
// "Probabilistic noninterference for multi-threaded programs";
// Hedin-Sabelfeld 2012, "A perspective on information-flow control"
// (in NATO Science for Peace and Security Series D33, "Software
// Safety and Security: Tools for Analysis and Verification", IOS
// Press 2012, §4 — concurrency).  FIXY-FOUND-131: the paper is a
// tutorial/perspective in a NATO ASI volume, NOT a peer-reviewed
// survey — the title's literal word "perspective" signals as much.
// Sabelfeld-Myers 2003 (JSAC) is the canonical IFC survey cited at
// Entry 1 above.
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
        // FOUND-025: per-TU function-local-static eliminated; storage
        // now lives in a single inline-variable-template instantiation
        // keyed on this entry's (&name, &cite) NTTP function-pointer
        // pair, dedup'd across all TUs at link time.
        return ::crucible::fixy::theory::detail::kCorpusFullDiagnostic<
            &name, &cite>;
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
        // FOUND-025: per-TU function-local-static eliminated; storage
        // now lives in a single inline-variable-template instantiation
        // keyed on this entry's (&name, &cite) NTTP function-pointer
        // pair, dedup'd across all TUs at link time.
        return ::crucible::fixy::theory::detail::kCorpusFullDiagnostic<
            &name, &cite>;
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
        // FOUND-025: per-TU function-local-static eliminated; storage
        // now lives in a single inline-variable-template instantiation
        // keyed on this entry's (&name, &cite) NTTP function-pointer
        // pair, dedup'd across all TUs at link time.
        return ::crucible::fixy::theory::detail::kCorpusFullDiagnostic<
            &name, &cite>;
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
        // FOUND-025: per-TU function-local-static eliminated; storage
        // now lives in a single inline-variable-template instantiation
        // keyed on this entry's (&name, &cite) NTTP function-pointer
        // pair, dedup'd across all TUs at link time.
        return ::crucible::fixy::theory::detail::kCorpusFullDiagnostic<
            &name, &cite>;
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
        // FOUND-025: per-TU function-local-static eliminated; storage
        // now lives in a single inline-variable-template instantiation
        // keyed on this entry's (&name, &cite) NTTP function-pointer
        // pair, dedup'd across all TUs at link time.
        return ::crucible::fixy::theory::detail::kCorpusFullDiagnostic<
            &name, &cite>;
    }
};

// ── Entry 7: external_to_verified_without_attest ────────────────
//
// FIXY-FOUND-019: pre-019, the §30.14 corpus modeled the
// CONFIDENTIALITY axis (Bell-LaPadula "no write down") via entries
// 1/2/5/6, but the INTEGRITY DUAL (Biba 1977 "no write up", Clark-
// Wilson 1987 Transformation Procedure) had no corpus entry.  Closed
// here.
//
// Cite: Biba 1977 "Integrity Considerations for Secure Computer
// Systems" MITRE MTR-3153 — the integrity model dual to BLP.
// Where BLP forbids high→low confidentiality flows, Biba forbids
// low→high integrity flows.  Clark-Wilson 1987 "A Comparison of
// Commercial and Military Computer Security Policies" IEEE
// Symposium on Security and Privacy — replaces the lattice with
// Constrained Data Items (CDIs) maintained by Transformation
// Procedures (TPs) and Integrity Verification Procedures (IVPs);
// every untrusted-data-item (UDI) to CDI flow requires a TP that
// the auditor can identify.  Crucible's
// `grant::trust_assumed<Rationale>` IS the TP — a developer-
// authored, audit-grade documented bridge from low-integrity
// (source::External) to high-integrity (trust_verified) state.
//
// Pattern: a binding engages `from_source<safety::source::External>`
// (the canonical low-integrity provenance) AND `trust_verified` (the
// high-integrity sink claim) AND omits any `trust_assumed<Rationale>`
// grant (no documented attestation bridging the two).  The substrate
// admits this composition at the type level — `retag_policy<External,
// IntegrityVerified>` exists in Tagged.h — but every retag is a TP
// per Clark-Wilson, and a TP without a documented Rationale is the
// integrity-discipline analogue of an unaudited declassification.
//
// Why distinct from entries 1/2/5/6: those entries protect the
// Security axis (SecLevel::Public/Internal/Classified/Secret).
// Entry 7 protects the Trust axis (safety::trust::Verified vs
// Unverified) AND the Provenance axis (source::External vs
// source::Sanitized / IntegrityVerified).  An information-flow
// type system that catches only confidentiality but not integrity
// is sound for one direction of the IFC lattice and unsound for
// the other; the §30.14 corpus is now bidirectionally closed
// across the dual.
//
// Remediation: the user MUST EITHER (a) drop the `trust_verified`
// claim (output retains Unverified status — no integrity promise
// made), (b) drop the `from_source<External>` grant (binding does
// NOT consume raw untrusted input — the trust-verified claim flows
// from already-Sanitized state), OR (c) interpose
// `grant::trust_assumed<Rationale>` with a literal rationale
// documenting the integrity bridge — the auditor's grep target is
// "grant::trust_assumed<" per FOUND-038 audit-trail discipline.
//
// Companion future work: FOUND-020 (termination-channel covert
// flow, Askarov-Hunt-Sabelfeld-Sands 2008) and FOUND-022 (Type-
// aware corpus matchers — the present entry inspects only Grants;
// extending to `Tagged<T, source::External>` payload inspection is
// the FOUND-022 closure shape).

struct external_to_verified_without_attest {
    template <typename Type, typename... Grants>
    [[nodiscard]] static consteval bool matches() noexcept {
        const bool has_external =
            detail::has_grant_of<detail::is_external_source_grant, Grants...>();
        const bool has_verified =
            detail::has_grant_of<detail::is_trust_verified_grant, Grants...>();
        const bool has_attest =
            detail::has_grant_of<detail::is_trust_assumed_grant, Grants...>();
        return has_external && has_verified && !has_attest;
    }

    static constexpr std::string_view name() noexcept {
        return "external_to_verified_without_attest";
    }

    static constexpr std::string_view cite() noexcept {
        return "Biba 1977 'Integrity Considerations for Secure "
               "Computer Systems' MITRE MTR-3153 / Clark-Wilson 1987 "
               "'A Comparison of Commercial and Military Computer "
               "Security Policies' IEEE SP — integrity dual to BLP "
               "no-write-down: low-integrity source (External) flows "
               "into a high-integrity sink (trust_verified) without "
               "a documented Transformation Procedure attestation.  "
               "Insert grant::trust_assumed<Rationale> with a literal "
               "rationale documenting the integrity bridge, OR drop "
               "from_source<External> (input is already Sanitized), "
               "OR drop trust_verified (output retains Unverified).";
    }

    // fixy-A4-029: see classified_io_without_declassify::full_diagnostic.
    static constexpr std::string_view full_diagnostic() noexcept {
        // FOUND-025: per-TU function-local-static eliminated; storage
        // now lives in a single inline-variable-template instantiation
        // keyed on this entry's (&name, &cite) NTTP function-pointer
        // pair, dedup'd across all TUs at link time.
        return ::crucible::fixy::theory::detail::kCorpusFullDiagnostic<
            &name, &cite>;
    }
};

// ── Entry 8: secret_unbounded_termination_channel ────────────────
//
// FIXY-FOUND-020: pre-020, the §30.14 corpus modeled the data-flow
// channels (entries 1/2/5/6 BLP confidentiality, entry 3 ghost flow,
// entry 4 staleness/replay timing channel, entry 7 Biba/Clark-Wilson
// integrity dual) but lacked the termination channel — the covert
// channel that arises when an observer can detect whether a
// Secret-dependent computation diverges or terminates.
//
// Pattern: `as_secret` (or `as_classified`) on Security + `cost_
// unbounded` on Complexity + no `declassify<Policy>` whose
// `axes_discharged_of` covers `DischargeAxis::Termination`.
// Askarov-Hunt-Sabelfeld-Sands 2008 ESORICS proves that even under
// termination-insensitive noninterference (TINI), an attacker
// observing repeated executions can extract arbitrarily many bits
// of a high-classified secret by encoding it in the program's
// termination behavior — "leaks more than just a bit" defeats the
// TINI rationale that "non-termination is unobservable."
//
// Remediation: drop `cost_unbounded` (claim bounded Complexity —
// the function actually terminates on all inputs), drop the Secret
// engagement (project Security to as_public / as_unclassified), or
// (future) interpose a declassify policy whose `axes_discharged_of`
// covers `DischargeAxis::Termination`.  No currently-shipped policy
// lifts the Termination bit — its reservation in the
// `DischargeAxis` enum holds the slot per Hunt-Sands safe-default
// "axis-matched authority required" rule.

struct secret_unbounded_termination_channel {
    template <typename Type, typename... Grants>
    [[nodiscard]] static consteval bool matches() noexcept {
        // FOUND-022 strengthening: include type-level Secret<T> as
        // a secret-engagement signal parallel to grant-level
        // is_secret_grant.  A Secret<T>-wrapped payload IS a
        // type-level Security claim even without an as_secret
        // grant — the AHSS termination channel fires on Secret
        // payload + cost_unbounded just as it does on as_secret
        // grant + cost_unbounded.
        const bool has_secret =
            detail::has_grant_of<detail::is_secret_grant, Grants...>()
            || detail::is_secret_type<Type>::value;
        const bool has_unbounded =
            detail::has_grant_of<detail::is_cost_unbounded_grant,
                                 Grants...>();
        const bool has_termination_discharge =
            detail::has_declassify_for_axis<
                detail::DischargeAxis::Termination, Grants...>();
        return has_secret && has_unbounded && !has_termination_discharge;
    }

    static constexpr std::string_view name() noexcept {
        return "secret_unbounded_termination_channel";
    }

    static constexpr std::string_view cite() noexcept {
        return "Askarov-Hunt-Sabelfeld-Sands 2008 'Termination-"
               "Insensitive Noninterference Leaks More Than Just a "
               "Bit' (ESORICS) — PRIMARY: formalizes the covert "
               "termination channel.  Under termination-insensitive "
               "noninterference, an observer of repeated executions "
               "can extract arbitrarily many bits of a Secret by "
               "encoding it in the program's termination decisions; "
               "the classical TINI rationale that 'non-termination "
               "is unobservable' fails because real-world observers "
               "DO see whether processes complete.  Pattern: as_"
               "secret + cost_unbounded + no declassify policy whose "
               "axes_discharged_of covers DischargeAxis::Termination "
               "(no such policy currently ships — Termination is "
               "reserved per Hunt-Sands safe-default discipline).  "
               "Supporting: Volpano-Smith 1997 'A Type-Based "
               "Approach to Program Security' (CSFW) — extends the "
               "secure-flow type system to termination, motivating "
               "the bounded-Complexity proof obligation for "
               "Secret-dependent computations.  Remediation: drop "
               "grant::cost_unbounded (claim bounded Complexity, "
               "i.e. the function provably terminates on all "
               "inputs), OR drop the Secret engagement (project to "
               "as_public / as_unclassified), OR (future) interpose "
               "declassify<Policy> where Policy's axes_discharged_of "
               "lifts DischargeAxis::Termination.";
    }

    // fixy-A4-029: see classified_io_without_declassify::full_diagnostic.
    static constexpr std::string_view full_diagnostic() noexcept {
        // FOUND-025: per-TU function-local-static eliminated; storage
        // now lives in a single inline-variable-template instantiation
        // keyed on this entry's (&name, &cite) NTTP function-pointer
        // pair, dedup'd across all TUs at link time.
        return ::crucible::fixy::theory::detail::kCorpusFullDiagnostic<
            &name, &cite>;
    }
};

// ── Entry 9: secret_payload_without_security_claim ─────────────────
//
// FIXY-FOUND-022: pre-022 the §30.14 corpus matchers were all
// signature `<typename Type, typename... Grants>` but their bodies
// inspected only Grants — Type was structurally ignored.  A binding
// whose payload IS `safety::Secret<U>` (the value type itself
// encodes secrecy) passed every matcher unless ALSO engaging
// Security via grants.  This entry closes the opacity gap.
//
// Pattern: `is_secret_type<Type>` (payload is Secret-wrapped) +
// NO Security-axis engagement via grants (no as_secret, no
// as_classified, no declassify<Policy>, no
// accept_default_strict_for<Security>).  The binding's PAYLOAD
// says "classified" but the binding's POLICY says nothing —
// information flows through the Secret-wrapped value without an
// audit trail because the type system never registers the
// classification.
//
// Cite: Sabelfeld-Sands 2009 'Declassification: Dimensions and
// Principles' (J. Computer Security) — the "what" dimension
// (which data is classified) MUST be made explicit at the
// policy level, not inferred from in-band metadata, because
// classification policies that depend on payload inspection are
// not statically checkable.  Sabelfeld-Myers 2003 reinforces:
// information-flow policies are a TYPE-LEVEL artifact, not a
// runtime-introspection artifact; a Secret<T> payload without a
// Security-axis grant declares the type-level policy is silent
// where the wrapper says otherwise.
//
// Remediation: EITHER (a) engage Security explicitly via
// `as_secret` / `as_classified` (declare the classification at
// the policy level), OR (b) unwrap the Secret<T> payload via
// `declassify<Policy>` BEFORE the binding boundary (the payload
// type then reflects the declassified data), OR (c) project
// Security explicitly to `as_public` / `as_unclassified` with a
// rationale (the payload IS in a Secret<T> wrapper but the
// binding asserts the policy permits leakage — strange but
// audit-traceable).  The corpus tier-5 rejection forces the
// developer to make the type-level / wrapper-level discrepancy
// EXPLICIT rather than silent.

// ── Entry 10: secret_catastrophic_staleness ────────────────────────
//
// FIXY-FOUND-024: pre-024 the §30.14 corpus matched stale_to<N>
// regardless of N's magnitude — stale_to<5> and stale_to<10000>
// both fired entry 4 (staleness_secret_without_declassify) and both
// silenced on declassify<AuthorizedReplay>.  This binary treatment
// obscured the qualitative difference between mild replay windows
// (acceptable under AuthorizedReplay) and catastrophic windows
// (over a thousand generations of replay surface, which carries
// qualitatively different risk).  Entry 10 closes the gradient via
// the FOUND-024 magnitude predicate.
//
// Pattern: secret engagement (via is_secret_grant Grants OR
// is_secret_type<Type> per FOUND-022 strengthening) +
// any_stale_to_at_least<kStaleToCatastrophic, Grants...>() (TauMax
// >= 1024) + no declassify<Policy> whose axes_discharged_of covers
// DischargeAxis::CatastrophicReplay (which AuthorizedReplay does
// NOT lift — see CatastrophicReplay reservation in DischargeAxis
// enum).
//
// Cite: Askarov-Hunt-Sabelfeld-Sands 2008 ESORICS — the replay
// channel formalization is structurally equivalent to the
// termination channel: an observer of repeated executions extracts
// arbitrarily many bits of a Secret by exploiting the replay
// surface.  Window magnitude matters because a larger window
// admits more observations per attack iteration, accelerating the
// extraction.  Sabelfeld-Sands 2009 "Declassification: Dimensions
// and Principles" supports the gradient-soft policy: the "when"
// dimension (temporal authorization) can have multiple
// authorization tiers — a small replay window is authorizable by
// AuthorizedReplay; a catastrophic window requires a stricter
// policy that this corpus reserves the discharge bit for.
//
// Remediation: (a) lower the stale_to<N> grant to N < 1024 (the
// kStaleToCatastrophic threshold), routing through entry 4's
// normal AuthorizedReplay discharge path, OR (b) drop the Secret
// engagement (project Security to as_public / as_unclassified —
// note this still fires entry 9 per FOUND-022 raw-declassification
// guard), OR (c) declassify<Policy> where Policy's
// axes_discharged_of lifts DischargeAxis::CatastrophicReplay (no
// such policy currently ships — Termination-style reservation per
// Hunt-Sands safe-default).  The threshold (1024 generations) is a
// conservative default per FOUND-024 doc-block and future tuning
// should override via fixy-config (not yet shipped).

struct secret_catastrophic_staleness {
    template <typename Type, typename... Grants>
    [[nodiscard]] static consteval bool matches() noexcept {
        // FOUND-022 strengthening: type-level Secret<T> AND
        // grant-level secrecy claim both count as engagement.
        const bool has_secret =
            detail::has_grant_of<detail::is_secret_grant, Grants...>()
            || detail::is_secret_type<Type>::value;
        // FOUND-024 magnitude predicate: catastrophic window
        // (TauMax >= 1024) AT THE TYPE LEVEL.
        const bool has_catastrophic_stale =
            detail::any_stale_to_at_least<
                detail::kStaleToCatastrophic, Grants...>();
        // FOUND-024 discharge: AuthorizedReplay lifts Staleness but
        // NOT CatastrophicReplay; entry 10 requires the latter.
        const bool has_catastrophic_discharge =
            detail::has_declassify_for_axis<
                detail::DischargeAxis::CatastrophicReplay,
                Grants...>();
        return has_secret && has_catastrophic_stale &&
               !has_catastrophic_discharge;
    }

    static constexpr std::string_view name() noexcept {
        return "secret_catastrophic_staleness";
    }

    static constexpr std::string_view cite() noexcept {
        return "Askarov-Hunt-Sabelfeld-Sands 2008 'Termination-"
               "Insensitive Noninterference Leaks More Than Just a "
               "Bit' (ESORICS) — PRIMARY: the replay channel "
               "formalization is structurally equivalent to the "
               "termination channel.  An observer of repeated "
               "executions extracts arbitrarily many bits of a "
               "Secret by exploiting the replay surface, and the "
               "extraction rate scales with the replay window "
               "magnitude.  A catastrophic window (N >= 1024 "
               "generations of stale replay) carries qualitatively "
               "different attack-amortization risk than a mild "
               "window (N < 1024) and is held to a stricter "
               "discharge policy.  Supporting: Sabelfeld-Sands "
               "2009 'Declassification: Dimensions and Principles' "
               "(J. Computer Security) — the 'when' dimension "
               "(temporal authorization) admits a graded policy "
               "structure; AuthorizedReplay authorizes mild "
               "windows (any N via Staleness axis), but a "
               "catastrophic window requires a stronger policy "
               "(DischargeAxis::CatastrophicReplay, reserved here, "
               "no shipping policy lifts it yet).  Remediation: (a) "
               "lower stale_to<N> to N < 1024 (route through entry "
               "4's normal AuthorizedReplay discharge), (b) drop "
               "the Secret engagement (note: as_public still fires "
               "entry 9 per FOUND-022 raw-declassification guard), "
               "OR (c) future declassify<Policy> with "
               "axes_discharged_of covering CatastrophicReplay.";
    }

    // fixy-A4-029: see classified_io_without_declassify::full_diagnostic.
    static constexpr std::string_view full_diagnostic() noexcept {
        // FOUND-025: per-TU function-local-static eliminated; storage
        // now lives in a single inline-variable-template instantiation
        // keyed on this entry's (&name, &cite) NTTP function-pointer
        // pair, dedup'd across all TUs at link time.
        return ::crucible::fixy::theory::detail::kCorpusFullDiagnostic<
            &name, &cite>;
    }
};

struct secret_payload_without_security_claim {
    template <typename Type, typename... Grants>
    [[nodiscard]] static consteval bool matches() noexcept {
        const bool type_is_secret =
            detail::is_secret_type<Type>::value;
        const bool has_security_engagement =
            detail::has_grant_of<detail::is_secret_grant, Grants...>();
        return type_is_secret && !has_security_engagement;
    }

    static constexpr std::string_view name() noexcept {
        return "secret_payload_without_security_claim";
    }

    static constexpr std::string_view cite() noexcept {
        return "Sabelfeld-Sands 2009 'Declassification: Dimensions "
               "and Principles' (J. Computer Security) — PRIMARY: "
               "names the 'what' dimension of declassification and "
               "argues classification policies MUST be statically "
               "declared at the type level, not inferred from "
               "runtime payload introspection.  A safety::Secret<T> "
               "payload without a corresponding Security-axis grant "
               "is a silent type-level / wrapper-level divergence: "
               "the value type encodes classification ('this is "
               "Secret') while the binding's policy pack stays "
               "silent on Security.  Supporting: Sabelfeld-Myers "
               "2003 'Language-based Information-flow Security' "
               "(IEEE J. Sel. Areas) — information-flow policies "
               "are a TYPE-LEVEL artifact; classification through "
               "wrapper-only encoding is not statically checkable "
               "and admits silent policy drift.  Remediation: (a) "
               "engage Security explicitly via grant::as_secret or "
               "grant::as_classified (declare the classification at "
               "the policy level), (b) unwrap the Secret<T> via "
               "grant::declassify<Policy> BEFORE the binding "
               "boundary (the payload type reflects declassified "
               "data).  Note: downward Security projection via "
               "grant::as_public / as_unclassified is NOT a valid "
               "silencing remediation — `is_secret_grant` (the "
               "Security-engagement detector consumed here) "
               "intentionally matches only secrecy-raising or "
               "declassify-related forms, so adding as_public to a "
               "Secret<T>-wrapped payload still fires this entry as "
               "raw declassification (the formal audit channel is "
               "declassify<Policy>, not silent downward projection).";
    }

    // fixy-A4-029: see classified_io_without_declassify::full_diagnostic.
    static constexpr std::string_view full_diagnostic() noexcept {
        // FOUND-025: per-TU function-local-static eliminated; storage
        // now lives in a single inline-variable-template instantiation
        // keyed on this entry's (&name, &cite) NTTP function-pointer
        // pair, dedup'd across all TUs at link time.
        return ::crucible::fixy::theory::detail::kCorpusFullDiagnostic<
            &name, &cite>;
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

namespace detail {

// ── FIXY-FOUND-021 closure: single source of corpus order ────────────
//
// Pre-FOUND-021, four hand-written if-chains (is_in_unsoundness_corpus,
// corpus_cite_for_v, corpus_entry_name_for_v, corpus_full_diagnostic_v)
// each listed all corpus entries in the same order.  The discipline
// burden: any maintainer adding/reordering entries had to update all
// four chains in lockstep.  Order drift between chains was a SILENT
// bug — the binding got rejected, but the rejection diagnostic
// surfaced the WRONG entry's name + cite.
//
// This tuple is now the SINGLE source of order.  All four chains
// iterate this tuple via `corpus_first_match_string_` (below) or a
// direct fold-expression.  Adding a new entry now means: (a) define
// the corpus struct, (b) append it here.  The four chains
// automatically pick up the new entry in the canonical order.  Order
// misalignment becomes STRUCTURALLY IMPOSSIBLE — there is no separate
// per-chain order to misalign.
//
// To bump corpus_size_v: append to this tuple AND increment
// corpus_size_v (sentinel below catches forgetting either side).
using CorpusEntries = std::tuple<
    corpus::classified_io_without_declassify,
    corpus::classified_bg_without_declassify,
    corpus::staleness_secret_without_declassify,
    corpus::ghost_runtime_observable,
    corpus::internal_io_without_declassify,
    corpus::internal_bg_without_declassify,
    corpus::external_to_verified_without_attest,        // FOUND-019 Biba/Clark-Wilson integrity dual
    corpus::secret_unbounded_termination_channel,       // FOUND-020 Askarov-Hunt termination channel
    corpus::secret_payload_without_security_claim,      // FOUND-022 payload-classification opacity closure
    corpus::secret_catastrophic_staleness               // FOUND-024 staleness magnitude gradient
>;

// Fold over CorpusEntries, returning Extractor(Entry) for the first
// matching Entry's accessor, or empty string_view if none match.
// Extractor is a templated lambda taking <typename E>() and returning
// std::string_view (the accessor selection — cite / name /
// full_diagnostic).
template <typename Type, typename... Grants, typename Extractor>
[[nodiscard]] consteval std::string_view
corpus_first_match_string_(Extractor extractor) noexcept {
    std::string_view result{};
    bool found = false;
    [&]<std::size_t... Is>(std::index_sequence<Is...>) consteval {
        auto check = [&]<typename E>() consteval {
            if (!found && E::template matches<Type, Grants...>()) {
                result = extractor.template operator()<E>();
                found = true;
            }
        };
        (check.template operator()<
            std::tuple_element_t<Is, CorpusEntries>>(), ...);
    }(std::make_index_sequence<std::tuple_size_v<CorpusEntries>>{});
    return result;
}

}  // namespace detail

template <typename Type, typename... Grants>
[[nodiscard]] consteval bool is_in_unsoundness_corpus() noexcept {
    // OR-fold over detail::CorpusEntries — short-circuits on first
    // match.  Replaces the hand-written 6-clause `||` chain; order
    // now derives from the tuple, eliminating FOUND-021 silent-bug
    // surface.
    return [&]<std::size_t... Is>(std::index_sequence<Is...>) consteval {
        return (std::tuple_element_t<Is, detail::CorpusEntries>
                    ::template matches<Type, Grants...>() || ...);
    }(std::make_index_sequence<std::tuple_size_v<detail::CorpusEntries>>{});
}

template <typename Type, typename... Grants>
inline constexpr bool IsInUnsoundnessCorpus_v =
    is_in_unsoundness_corpus<Type, Grants...>();

// ═════════════════════════════════════════════════════════════════════
// ── corpus_size_v — count sentinel for the maintenance protocol ────
// ═════════════════════════════════════════════════════════════════════
//
// fixy-L-09 / fixy-L-10: the corpus has SIX entries; every entry
// touches FOUR diagnostic chains.  This constant is the grep-able
// pin for the entry count — the std::array CTAD sentinel below
// derives its size from the initializer count (NOT from the
// declared bound), so drift in EITHER direction breaks the build:
//
//   (a) Bump `corpus_size_v` to 7 + forget to append the 7th entry
//       to `kRoster`  →  `kRoster.size() = 6 ≠ corpus_size_v = 7`
//       → static_assert fires.  (A C-style `T arr[N] = {…}` would
//       value-initialize the missing slots silently — std::array
//       CTAD makes the initializer-count load-bearing.)
//
//   (b) Append a 7th initializer to `kRoster` + forget to bump
//       `corpus_size_v`  →  `kRoster.size() = 7 ≠ corpus_size_v = 6`
//       → static_assert fires.
//
//   (c) Add a corpus struct missing one of {name, cite,
//       full_diagnostic} accessors  →  `kRoster` init fails to
//       compile at the missing-accessor call site.  Exercising all
//       three accessors per entry (via the `entry_witness` row
//       below) closes the "added struct with `name()` but forgot
//       `cite()` / `full_diagnostic()`" drift class right at the
//       roster, NOT three chains away.
//
// To bump: increment `corpus_size_v` AND append a new
// `entry_witness{...::name(), ...::cite(), ...::full_diagnostic()}`
// row.  Forgetting either side is a hard compile error.  The
// 5-step PR shape lives in the Discipline block at the top of this
// header.

inline constexpr std::size_t corpus_size_v = 10;

namespace detail::corpus_size_sentinel {

// One row per corpus entry — exercises the three string-returning
// accessors that the cite/name/full if-chains consume.  Missing
// any accessor on a corpus struct surfaces as a compile error at
// the roster initializer below (in addition to the chain-side
// failure), so the discipline gap is visible in ONE place.
struct entry_witness {
    std::string_view name;
    std::string_view cite;
    std::string_view full;
};

// CTAD: size deduced from initializer count.  `kRoster.size()`
// returns 6 because there are 6 row-initializers, regardless of
// what `corpus_size_v` is declared to be.  This is what makes the
// drift static_assert below load-bearing rather than tautological.
inline constexpr std::array kRoster = {
    entry_witness{corpus::classified_io_without_declassify::name(),
                  corpus::classified_io_without_declassify::cite(),
                  corpus::classified_io_without_declassify::full_diagnostic()},
    entry_witness{corpus::classified_bg_without_declassify::name(),
                  corpus::classified_bg_without_declassify::cite(),
                  corpus::classified_bg_without_declassify::full_diagnostic()},
    entry_witness{corpus::staleness_secret_without_declassify::name(),
                  corpus::staleness_secret_without_declassify::cite(),
                  corpus::staleness_secret_without_declassify::full_diagnostic()},
    entry_witness{corpus::ghost_runtime_observable::name(),
                  corpus::ghost_runtime_observable::cite(),
                  corpus::ghost_runtime_observable::full_diagnostic()},
    entry_witness{corpus::internal_io_without_declassify::name(),
                  corpus::internal_io_without_declassify::cite(),
                  corpus::internal_io_without_declassify::full_diagnostic()},
    entry_witness{corpus::internal_bg_without_declassify::name(),
                  corpus::internal_bg_without_declassify::cite(),
                  corpus::internal_bg_without_declassify::full_diagnostic()},
    entry_witness{corpus::external_to_verified_without_attest::name(),
                  corpus::external_to_verified_without_attest::cite(),
                  corpus::external_to_verified_without_attest::full_diagnostic()},
    entry_witness{corpus::secret_unbounded_termination_channel::name(),
                  corpus::secret_unbounded_termination_channel::cite(),
                  corpus::secret_unbounded_termination_channel::full_diagnostic()},
    entry_witness{corpus::secret_payload_without_security_claim::name(),
                  corpus::secret_payload_without_security_claim::cite(),
                  corpus::secret_payload_without_security_claim::full_diagnostic()},
    entry_witness{corpus::secret_catastrophic_staleness::name(),
                  corpus::secret_catastrophic_staleness::cite(),
                  corpus::secret_catastrophic_staleness::full_diagnostic()},
};
static_assert(kRoster.size() == corpus_size_v,
    "fixy-L-09/L-10: corpus_size_v drifted from the actual entry "
    "roster.  Re-audit the OR fold in is_in_unsoundness_corpus, the "
    "if-chains in corpus_cite_for_v / corpus_entry_name_for_v / "
    "corpus_full_diagnostic_v, AND this roster array.  The "
    "discipline block at the top of Theory.h enumerates the "
    "5-step PR shape.");

// Roster entries must each surface a NON-EMPTY name + cite — every
// corpus struct ships these via `define_static_string` literals.
// An empty view would indicate either (a) a malformed entry struct
// or (b) a `define_static_string` linkage problem.  The full
// diagnostic IS allowed to be empty under specific structural
// conditions (see corpus::ghost_runtime_observable doc-block), so
// we only constrain name + cite here.
static_assert([] consteval {
    for (auto const& w : kRoster) {
        if (w.name.empty() || w.cite.empty()) { return false; }
    }
    return true;
}(), "fixy-L-09/L-10: a corpus entry shipped empty name() or "
     "cite() — every entry must surface a non-empty diagnostic "
     "string for the tier-5 rejection message.");

}  // namespace detail::corpus_size_sentinel

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

// FIXY-FOUND-021: iterates detail::CorpusEntries (single source of
// order) — replaces the hand-written 6-clause if-chain that was
// silent-bug-prone vs the OR-fold's order.
template <typename Type, typename... Grants>
inline constexpr std::string_view corpus_cite_for_v =
    detail::corpus_first_match_string_<Type, Grants...>(
        []<typename E>() consteval -> std::string_view { return E::cite(); });

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

// FIXY-FOUND-021: iterates detail::CorpusEntries (single source of order).
template <typename Type, typename... Grants>
inline constexpr std::string_view corpus_entry_name_for_v =
    detail::corpus_first_match_string_<Type, Grants...>(
        []<typename E>() consteval -> std::string_view { return E::name(); });

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

// FIXY-FOUND-021: iterates detail::CorpusEntries (single source of order).
// No corpus match → empty string_view (tier-5 succeeds; message unused).
template <typename Type, typename... Grants>
inline constexpr std::string_view corpus_full_diagnostic_v =
    detail::corpus_first_match_string_<Type, Grants...>(
        []<typename E>() consteval -> std::string_view {
            return E::full_diagnostic();
        });

}  // namespace crucible::fixy::theory
