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
// `IsAcceptedFn<Type, Grants...>` (Reject.h, §IsAccepted full gate)
// composes engagement + well-formedness + uniqueness +
// `NotInTheoryCorpus<Type, Grants...>` (this header).  A binding
// that engages every axis correctly STILL fails IsAccepted if its
// Grants pack matches any §30.14 entry.  The fixy-level diagnostic
// names which corpus entry matched (paper + year).
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

#include <cstddef>
#include <type_traits>

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

template <typename G> struct is_io_effect_grant
    : std::false_type {};
template <effects::Effect... Es>
struct is_io_effect_grant<grant::with<Es...>>
    : std::bool_constant<((Es == effects::Effect::IO) || ...)> {};

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
// footprint: Alloc, IO, Block, or Bg.  (Init and Test are compile-
// time / one-shot tags that ghost code is allowed to coexist with —
// the discipline only bans runtime presence.)
template <typename G> struct is_observable_effect_grant
    : std::false_type {};
template <effects::Effect... Es>
struct is_observable_effect_grant<grant::with<Es...>>
    : std::bool_constant<(
        ((Es == effects::Effect::Alloc) ||
         (Es == effects::Effect::IO)    ||
         (Es == effects::Effect::Block) ||
         (Es == effects::Effect::Bg))   || ...)> {};

}  // namespace detail

// ═════════════════════════════════════════════════════════════════════
// ── §30.14 Corpus — one detector per known-broken pattern ──────────
// ═════════════════════════════════════════════════════════════════════

namespace corpus {

// ── Entry 1: classified_io_without_declassify ─────────────────────
//
// Cite: §30.14 implicit-flow row.  Volpano-Smith-Irvine 1996, "A
// sound type system for secure flow analysis"; Sabelfeld-Myers 2003,
// "Language-based information-flow security".
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

    static constexpr const char* cite() noexcept {
        return "Volpano-Smith-Irvine 1996 / Sabelfeld-Myers 2003 — "
               "implicit information flow: classified value flows out "
               "of the program via I/O without a declassification "
               "policy.  Insert grant::declassify<Policy> with a named "
               "policy OR drop the IO effect.";
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

    static constexpr const char* cite() noexcept {
        return "Smith-Volpano 1998 / Sabelfeld-Sands 2000 / "
               "Hedin-Sabelfeld 2012 — concurrent information flow: "
               "classified value crosses into a background-thread "
               "context without a declassification policy; the spawn "
               "is itself a scheduler-observable event.  Insert "
               "grant::declassify<Policy> OR drop the Bg effect OR "
               "project Security to a less restrictive level.";
    }
};

// ── Entry 3: staleness_secret_without_declassify ─────────────────
//
// Cite: Sabelfeld-Sands 2009, "Declassification: dimensions and
// principles" (J. Computer Security) — the "when" dimension of
// declassification (temporal release authorization); Hunt-Sands
// 2008, "Just Forget It — The Semantics and Enforcement of
// Information Erasure" (POPL) — the dual axis: classified data
// that should be unobservable after a staleness window expires
// is a failed erasure policy.  (fixy-CR-16: replaces a prior
// Andrysco-et-al 2015 attribution that did not back the claim —
// that paper, "On Subnormal Floating Point and Abnormal Timing"
// IEEE S&P, is about FP-subnormal timing side channels and has
// nothing to say about staleness/freshness/replay information
// flow.  The substantive corpus entry is unchanged; only the
// supporting citation rotates to the correct paper.)
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
// OR (c) interpose `declassify<Policy>` with a named policy
// authorizing the staleness-window flow.  The substrate's CtCrypto
// stance is the canonical (b) form (pins Staleness=Fresh).

struct staleness_secret_without_declassify {
    template <typename Type, typename... Grants>
    [[nodiscard]] static consteval bool matches() noexcept {
        const bool has_secret =
            detail::has_grant_of<detail::is_secret_grant, Grants...>();
        const bool has_stale =
            detail::has_grant_of<detail::is_stale_grant, Grants...>();
        const bool has_declassify =
            detail::has_grant_of<detail::is_declassify_grant, Grants...>();
        return has_secret && has_stale && !has_declassify;
    }

    static constexpr const char* cite() noexcept {
        return "Sabelfeld-Sands 2009 / Hunt-Sands 2008 — "
               "stale-replay as failed erasure: classified value is "
               "reachable through a non-Fresh Staleness window "
               "(stale_to<TauMax>) without a declassification "
               "policy; the replay-window keeps observable what an "
               "erasure policy would require be forgotten.  Insert "
               "grant::declassify<Policy> OR drop the stale_to<N> "
               "grant (Staleness defaults to Fresh) OR project "
               "Security to a less restrictive level.";
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

    static constexpr const char* cite() noexcept {
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
        || corpus::ghost_runtime_observable::matches<Type, Grants...>();
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

}  // namespace crucible::fixy::theory
