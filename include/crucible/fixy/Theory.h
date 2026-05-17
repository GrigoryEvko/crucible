#pragma once

// ── crucible::fixy — Theory.h — §30.14 known-unsoundness corpus ────
//
// Phase D of the fixy reimplementation per misc/16_05_2026_fixy.md
// §4.  This is the LOAD-BEARING reject-by-default surface that closes
// the FX §30.14 type-theory unsoundness corpus.  Every entry pairs:
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

#include <crucible/fixy/Grant.h>

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
    return corpus::classified_io_without_declassify::matches<Type, Grants...>();
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
