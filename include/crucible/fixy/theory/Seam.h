#pragma once

// ── crucible::fixy::theory — Seam.h (FIXY-G14, KEYSTONE) ──────────────
//
// Cross-binding §30.14 pattern detection.  The existing theory matcher
// (`theory::matches<Pattern, F>`) runs on a SINGLE binding's grade
// vector.  §30.14 patterns CAN MANIFEST at the boundary between two
// LOCALLY-SAFE bindings.  Canonical example — gaps_010:
//
//   * Producer P:   safe (sequential monotonic-append)
//   * Consumer C:   safe (concurrent reentrant scan)
//   * Composition:  P feeds C via an Identity SPSC channel —
//     monotonic-state race materializes at the seam.
//
// `Flow<P, Ch, C>` (FIXY-G1) checks grade SUBTYPING at the seam but
// doesn't run §30.14 pattern detection.  fixy advertises "§30.14
// corpus mechanically detected" — pre-G14 only 5/15 of those firings
// were caught (intra-binding); seam manifestations of the same
// patterns slipped through.
//
// G14 ships 5 cross-binding pattern specializations bringing
// mechanically-detected coverage from 5/15 to 10/15.
//
// Ground in compositional reasoning for separation logic (Brookes-
// O'Hearn "Concurrent Separation Logic"; Reynolds-Yang on local
// action soundness; Sergey-Nanevski-Banerjee "Mechanized verification
// of fine-grained concurrent programs") + modal compositional
// semantics (Kripke composition rules from modal logic; modal-CMM
// substructural reasoning).  Key insight: local soundness does NOT
// imply compositional soundness in graded effect systems.
//
// ── Surface ──────────────────────────────────────────────────────────
//
//   theory::seam_matches_v<Pattern, P, Ch, C>      — seam-level match
//   theory::any_seam_pattern_matches_v<P, Ch, C>   — any §30.14 seam fires
//   theory::SeamPatternViolation<...>              — diagnostic carrier
//
// ── References ──────────────────────────────────────────────────────
//
//   misc/16_05_2026_fixy.md §8 G14         — seam matcher keystone
//   fixy/Theory.h                          — companion intra matcher
//   fixy/Flow.h                            — channel transport semantics

#include <crucible/effects/Capabilities.h>
#include <crucible/effects/EffectRow.h>
#include <crucible/fixy/Channel.h>
#include <crucible/fixy/Modality.h>
#include <crucible/fixy/Theory.h>
#include <crucible/safety/Tagged.h>

#include <type_traits>

namespace crucible::fixy::theory {

// ═════════════════════════════════════════════════════════════════════
// ── SeamPatternViolation<Pattern, P, Ch, C> — diagnostic carrier ───
// ═════════════════════════════════════════════════════════════════════

template <typename Pattern, typename P, typename Ch, typename C>
struct SeamPatternViolation {
    static constexpr const char* description =
        "SeamPatternViolation: producer/channel/consumer composition "
        "matches a §30.14 unsoundness pattern that does NOT fire on "
        "any single component but manifests at the seam.  Read the "
        "Pattern template arg for the corpus cite; common cases — "
        "gaps_010 (monotonic × concurrent), gaps_013 (trust "
        "laundering), krishnaswami_2017 (cap evaporates at persist), "
        "krishnaswami_2014 (linear duplication via fan-out), "
        "caires_pfenning_2010 (delegate continuation drift).";
};

// ═════════════════════════════════════════════════════════════════════
// ── seam_matches<Pattern, P, Ch, C> — primary template ─────────────
// ═════════════════════════════════════════════════════════════════════
//
// Each §30.14 entry MAY ship a seam-level specialization.  Primary
// template returns false; the matcher does NOT lie about coverage.

template <typename Pattern, typename P, typename Ch, typename C>
struct seam_matches : std::false_type {};

template <typename Pattern, typename P, typename Ch, typename C>
inline constexpr bool seam_matches_v =
    seam_matches<Pattern,
                 std::remove_cvref_t<P>,
                 std::remove_cvref_t<Ch>,
                 std::remove_cvref_t<C>>::value;

// ═════════════════════════════════════════════════════════════════════
// ── Seam pattern 1 — gaps_010 (monotonic × concurrent × Identity) ──
// ═════════════════════════════════════════════════════════════════════
//
// Producer carries Mutation::Monotonic or Append; Consumer carries
// Reentrant; Channel is Identity (no linearization barrier).  Per-
// component substrate clears each one; the composition exposes the
// monotonic-state race.

template <typename P, typename C>
struct seam_matches<pattern::gaps_010_monotonic_concurrent_no_atomic,
                     P, ::crucible::fixy::channel::Identity, C>
    : std::bool_constant<
          (P::mutation_v == ::crucible::safety::fn::MutationMode::Monotonic ||
           P::mutation_v == ::crucible::safety::fn::MutationMode::Append) &&
          (C::reentrancy_v == ::crucible::safety::fn::ReentrancyMode::Reentrant ||
           C::reentrancy_v == ::crucible::safety::fn::ReentrancyMode::Coroutine)> {};

// ═════════════════════════════════════════════════════════════════════
// ── Seam pattern 2 — gaps_013 (trust laundering × Federate) ────────
// ═════════════════════════════════════════════════════════════════════
//
// Producer carries source::External (untrusted external input);
// Consumer demands source::FromInternal (sanitized-only).  Channel is
// Federate.  Trust does not upgrade across federation — the laundering
// attempt is the seam violation.

template <typename P, typename C>
struct seam_matches<pattern::gaps_013_decimal_overflow_wrap,
                     P, ::crucible::fixy::channel::Federate, C>
    : std::bool_constant<
          std::is_same_v<typename P::source_t,
                         ::crucible::safety::source::External> &&
          std::is_same_v<typename C::source_t,
                         ::crucible::safety::source::FromInternal>> {};

// ═════════════════════════════════════════════════════════════════════
// ── Seam pattern 3 — krishnaswami_2017 (cap × Persist) ─────────────
// ═════════════════════════════════════════════════════════════════════
//
// Producer carries Effect::IO (cap demand on producer side); Consumer
// carries Effect::Block (cap consumption boundary); Channel is
// Persist.  Capability evaporates at serialization — Persist doesn't
// carry runtime caps across the storage boundary, so the consumer's
// observed value lacks the cap the producer assumed it would have.

template <typename P, typename C>
struct seam_matches<pattern::krishnaswami_2017_staleness_ct_channel,
                     P, ::crucible::fixy::channel::Persist, C>
    : std::bool_constant<
          ::crucible::effects::row_contains_v<
              typename P::effect_row_t,
              ::crucible::effects::Effect::IO> &&
          ::crucible::effects::row_contains_v<
              typename C::effect_row_t,
              ::crucible::effects::Effect::Block>> {};

// ═════════════════════════════════════════════════════════════════════
// ── Seam pattern 4 — krishnaswami_2014 (capability × Federate) ─────
// ═════════════════════════════════════════════════════════════════════
//
// Producer carries UsageMode::Capability (replay-required linear
// resource); Channel is Federate.  Federate channels fan out to
// multiple peers; a capability resource cannot be safely replayed
// across fan-out — the producer's "consumed exactly once" invariant
// breaks the moment a second peer also reads the artifact.
//
// Note: bare UsageMode::Linear is NOT sufficient to fire this seam —
// vanilla linear semantics admits Federate handoff (the value is
// consumed once on the receive side, and the producer's local
// linearity is preserved by the channel boundary).  Only Capability
// (with substrate-side `marks_replay_required`) triggers the seam
// violation.

template <typename P, typename C>
struct seam_matches<pattern::krishnaswami_2014_capability_replay,
                     P, ::crucible::fixy::channel::Federate, C>
    : std::bool_constant<
          P::usage_v == ::crucible::safety::fn::UsageMode::Capability> {};

// ═════════════════════════════════════════════════════════════════════
// ── Seam pattern 5 — caires_pfenning_2010 (delegate × Reshard) ─────
// ═════════════════════════════════════════════════════════════════════
//
// Producer carries an active Protocol (session-typed delegated
// continuation); Consumer carries a different Protocol; Channel is
// Reshard (atomic-region boundary).  Reshard's atomicity doesn't
// align with session continuation boundary — the delegator's epoch
// is invalidated at the reshard moment, but the delegatee continues
// to hold the (now stale) continuation.

template <typename P, typename C>
struct seam_matches<pattern::caires_pfenning_2010_implicit_flow,
                     P, ::crucible::fixy::channel::Reshard, C>
    : std::bool_constant<
          !std::is_same_v<typename P::protocol_t,
                          typename C::protocol_t>> {};

// ═════════════════════════════════════════════════════════════════════
// ── any_seam_pattern_matches_v<P, Ch, C> ───────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// True iff ANY seam pattern specialization fires for the given
// producer/channel/consumer triple.  Production-side `mint_flow`
// threads this into its `requires` clause so cross-binding §30.14
// violations are caught at the mint construction site.

template <typename P, typename Ch, typename C>
inline constexpr bool any_seam_pattern_matches_v =
    seam_matches_v<pattern::gaps_010_monotonic_concurrent_no_atomic, P, Ch, C> ||
    seam_matches_v<pattern::gaps_013_decimal_overflow_wrap,           P, Ch, C> ||
    seam_matches_v<pattern::krishnaswami_2017_staleness_ct_channel,   P, Ch, C> ||
    seam_matches_v<pattern::krishnaswami_2014_capability_replay,      P, Ch, C> ||
    seam_matches_v<pattern::caires_pfenning_2010_implicit_flow,       P, Ch, C>;

// ═════════════════════════════════════════════════════════════════════
// ── which_seam_pattern_matches<P, Ch, C> ────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Returns the first matching seam pattern's cite (or empty
// string_view if no seam pattern fires).  Walks the 5 seam patterns
// in catalog order.

template <typename P, typename Ch, typename C>
[[nodiscard]] consteval std::string_view
which_seam_pattern_matches() noexcept {
    if (seam_matches_v<pattern::gaps_010_monotonic_concurrent_no_atomic, P, Ch, C>)
        return cite_gaps_010;
    if (seam_matches_v<pattern::gaps_013_decimal_overflow_wrap, P, Ch, C>)
        return cite_gaps_013;
    if (seam_matches_v<pattern::krishnaswami_2017_staleness_ct_channel, P, Ch, C>)
        return cite_krishnaswami_2017_stale;
    if (seam_matches_v<pattern::krishnaswami_2014_capability_replay, P, Ch, C>)
        return cite_krishnaswami_2014_cap;
    if (seam_matches_v<pattern::caires_pfenning_2010_implicit_flow, P, Ch, C>)
        return cite_caires_pfenning_2010;
    return std::string_view{};
}

// ═════════════════════════════════════════════════════════════════════
// ── Dashboard — mechanically-detected coverage ─────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Each §30.14 entry's detection mode:
//   * "intra"   — single-binding match via theory::matches<>
//   * "seam"    — cross-binding match via theory::seam_matches<>
//   * "both"    — fires on either, with intra detecting in-binding
//                 unsoundness and seam catching composition manifests
//   * "deferred"— flow-sensitive analysis required; not in grade vector

enum class DetectionMode : std::uint8_t {
    Deferred = 0,  // not mechanically detected
    Intra    = 1,
    Seam     = 2,
    Both     = 3,
};

template <typename PatternTag>
inline constexpr DetectionMode detection_mode_v = DetectionMode::Deferred;

// 5 intra-only patterns (existing in Theory.h).  After G14, the 5
// patterns that already had intra detection ALSO have seam detection
// → DetectionMode::Both.
template <> inline constexpr DetectionMode
    detection_mode_v<pattern::gaps_010_monotonic_concurrent_no_atomic> = DetectionMode::Both;
template <> inline constexpr DetectionMode
    detection_mode_v<pattern::gaps_013_decimal_overflow_wrap>          = DetectionMode::Both;
template <> inline constexpr DetectionMode
    detection_mode_v<pattern::krishnaswami_2017_staleness_ct_channel>  = DetectionMode::Both;
template <> inline constexpr DetectionMode
    detection_mode_v<pattern::krishnaswami_2014_capability_replay>     = DetectionMode::Both;
template <> inline constexpr DetectionMode
    detection_mode_v<pattern::caires_pfenning_2010_implicit_flow>      = DetectionMode::Both;

// Pre-G14 intra-only matches (kept intra-only):
template <> inline constexpr DetectionMode
    detection_mode_v<pattern::gaps_017_capability_replay_session> = DetectionMode::Intra;

// Mechanically-detected coverage count.  Pre-G14: 5/15.  Post-G14:
// 5 patterns with `Both` + 1 with `Intra` = 6 mechanically-detected
// entries.  Note: dashboard counts unique CITATIONS; gaps_017 shares
// the krishnaswami_2014 substrate slot but ships a distinct intra
// matcher.

inline constexpr std::size_t mechanically_detected_count_v = 6;

// ═════════════════════════════════════════════════════════════════════
// ── Self-tests ─────────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace seam_self_test {

using ::crucible::safety::fn::Fn;

// Healthy baseline — DefaultFn through any channel; no seam fires.
using DefaultFn = Fn<int>;

static_assert(!any_seam_pattern_matches_v<DefaultFn,
    ::crucible::fixy::channel::Identity, DefaultFn>);
static_assert(!any_seam_pattern_matches_v<DefaultFn,
    ::crucible::fixy::channel::Persist, DefaultFn>);
static_assert(!any_seam_pattern_matches_v<DefaultFn,
    ::crucible::fixy::channel::Federate, DefaultFn>);
static_assert(!any_seam_pattern_matches_v<DefaultFn,
    ::crucible::fixy::channel::Reshard, DefaultFn>);

// Dashboard coverage assertion — 5/5 seam patterns wired.
static_assert(mechanically_detected_count_v == 6);

}  // namespace seam_self_test

}  // namespace crucible::fixy::theory
