#pragma once

// ── crucible::fixy::rule — R001..R020 alias projection ─────────────
//
// Per misc/16_05_2026_fixy.md §4.  Re-exports the 20 §6.8
// collision rule tags from safety/CollisionCatalog.h under the
// grep-discoverable `fixy::rule::R<NNN>` namespace.
//
// Convention: every production binding that wants to reference
// a §6.8 rule by its compact numeric code writes `fixy::rule::R001`
// (etc.) rather than the longer
// `safety::fn::collision::I002_ClassifiedFailPayload`.  The R<NNN>
// codes are stable across substrate refactors — adding a 21st rule
// shipped under `safety::fn::collision::*` requires only one
// additional `using R021 = ...;` line here.
//
// Mapping table (alphabetic letter prefix preserved as the substrate
// short-code; numeric code R<NNN> is order-of-shipping in fixy's
// catalog rollout):
//
//   R001  I002  classified × Fail payload
//   R002  L002  borrow × Async
//   R003  E044  constant-time × Async
//   R004  I003  constant-time × Fail on secret
//   R005  M012  Monotonic × concurrent (no atomic)
//   R006  P002  ghost × runtime use
//   R007  I004  classified × async × session
//   R008  N002  decimal × overflow(wrap)
//   R009  L003  borrow × unscoped spawn
//   R010  M011  linear × Fail (no cleanup)
//   R011  S010  Staleness × constant-time
//   R012  S011  Capability × Replay
//   R013  L004  linear lifetime needs Permission
//   R014  B001  Bg observable × bounded resource
//   R015  H001  hot path × bounded cost
//   R016  H002  hot path × witness floor
//   R017  L005  linear alias × same region tag (pack-level)
//   R018  F001  frame declares axis collision (pack-level)
//   R019  H003  hot path × terminating Alloc/IO
//   R020  F002  federation peer × terminating budget
//
// ── Substrate consumed ─────────────────────────────────────────────
//
//   safety::fn::collision::I002_ClassifiedFailPayload (and 19 peers)
//   safety::fn::collision::rule_code_of_v<Tag>          — bijection
//   safety::fn::collision::RuleCode                     — numeric enum
//
// ── Substrate added by this header ─────────────────────────────────
//
// NONE.  Twenty `using` aliases and a bijection self-test.  No new
// trait, no new diagnostic.  If a reviewer adds a rule tag
// here without also adding it to safety/CollisionCatalog.h, the
// static_asserts below fail because the substrate's RuleCode enum
// doesn't grow alongside.
//
// ── Cost ───────────────────────────────────────────────────────────
//
// Zero.  Type aliases compile to nothing.
//
// ── Self-test ──────────────────────────────────────────────────────
//
// Each alias rides through `rule_code_of_v<R<NNN>>` to confirm the
// substrate's bijection holds for the fixy::rule:: projection.  If a
// substrate rule is renamed without updating this header, the
// corresponding rule_code_of_v lookup fails to find the alias's
// underlying tag and the static_assert fires with the offending
// alias name.

#include <crucible/safety/CollisionCatalog.h>

namespace crucible::fixy::rule {

// ═════════════════════════════════════════════════════════════════════
// ── Pack-level helpers (FIXY-AUDIT-B7) ─────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Two §6.8 collision rules (L005 / F001) are inherently pack-level —
// they cannot be evaluated against a single Fn but only against the
// composition of two-or-more Fns sharing a frame.  The substrate
// ships these as variable templates under
// `safety::fn::collision::pack::*`; this section re-exports them
// under the `fixy::rule::` namespace so production callers can spell
// the predicate without crossing into the longer substrate name.
//
// Substrate authority:
//   safety::fn::collision::pack::no_linear_region_alias_v<Fs...>
//     — pairwise check that no two Fs share a `lifetime::In<Tag>`
//     with `usage_v == Linear`.  Backs R017 / L005.
//
//   safety::fn::collision::pack::frame_axis_consistent_v<Fs...>
//     — security-seed: every F in the pack agrees on
//     `security_v`.  Backs R018 / F001.
//
//   safety::fn::collision::pack::is_linear_in_region_v<F>
//     — per-Fn helper used by no_linear_region_alias.  Useful for
//     downstream metaprograms that want the same predicate.
//
//   safety::fn::collision::pack::same_region_tag_v<Tag1, Tag2>
//     — type-level region-tag identity check (extracted from
//     `region_tag_of_t<L>` carriers).
//
//   safety::fn::collision::pack::region_tag_of_t<L>
//     — lifetime → region-tag carrier extraction.  Returns void for
//     non-region lifetimes (lifetime::Static, etc.).
//
// ── Cost ───────────────────────────────────────────────────────────
//
// Zero.  Every entry is an alias or a variable-template forward.

namespace pack {

// ─── no_linear_region_alias_v — backs R017 / L005 ─────────────────
template <typename... Fs>
inline constexpr bool no_linear_region_alias_v =
    ::crucible::safety::fn::collision::pack::no_linear_region_alias_v<Fs...>;

// ─── frame_axis_consistent_v — backs R018 / F001 ──────────────────
template <typename... Fs>
inline constexpr bool frame_axis_consistent_v =
    ::crucible::safety::fn::collision::pack::frame_axis_consistent_v<Fs...>;

// ─── is_linear_in_region_v — per-Fn helper ────────────────────────
template <typename F>
inline constexpr bool is_linear_in_region_v =
    ::crucible::safety::fn::collision::pack::is_linear_in_region_v<F>;

// ─── same_region_tag_v — type-level tag identity ──────────────────
template <typename Tag1, typename Tag2>
inline constexpr bool same_region_tag_v =
    ::crucible::safety::fn::collision::pack::same_region_tag_v<Tag1, Tag2>;

// ─── region_tag_of_t — lifetime → region-tag carrier ──────────────
template <typename L>
using region_tag_of_t =
    typename ::crucible::safety::fn::collision::pack::region_tag_of<L>::type;

}  // namespace pack

// ═════════════════════════════════════════════════════════════════════
// ── FIXY-U-062: §6.8 ValidComposition + RuleCode aggregator surface
// ═════════════════════════════════════════════════════════════════════
//
// The 20 rule tags (R001..R020) below name the §6.8 collision rules,
// but the AGGREGATOR that decides whether a given `Fn<...>` instance
// passes all 20 simultaneously was previously reachable only via
// `safety::fn::ValidComposition<F>` (the `concept` gate at
// safety/Fn.h line 327) and `safety::fn::CollisionRules<F>` (the
// trait it consults).  Likewise `RuleCode` (the numeric enum
// projection of the rule tags) and its bijection helpers lived only
// inside the `safety::fn::collision::` namespace, requiring callers
// who wanted to query "which rule does this tag map to?" to descend
// into the substrate.
//
// This block surfaces the aggregator + bijection at `fixy::rule::`
// alongside the rule tags themselves, completing the §6.8 surface so
// callers can spell every layer of the composition contract through
// the fixy umbrella:
//
//   * RuleCode                  — numeric enum (20 entries + None)
//   * ValidComposition<F>       — primary `concept` gate (used by
//                                 `safety::Fn<...>` class-body
//                                 static_assert and by every direct
//                                 `Fn<Bad>` instantiation)
//   * CollisionRules<F>         — backing trait that ValidComposition
//                                 reads `::valid` from
//   * rule_code_of<Tag>         — Tag → RuleCode map (struct)
//   * rule_code_of_v<Tag>       — Tag → RuleCode (variable template)
//   * rule_tag_t<RuleCode>      — RuleCode → Tag (reverse map)
//   * rule_bijection_v<R>       — variable template asserting bijection
//   * I002_OK..F002_OK          — 20 per-rule concepts (positive form;
//                                 each evaluates true iff its rule's
//                                 collision predicate does NOT fire)
//   * AllRulesOK<F>             — aggregate of all 20 (== ValidComposition,
//                                 spelled as the conjunction for
//                                 debugging which rule fires)
//
// ── Why per-rule concepts ─────────────────────────────────────────
//
// Direct `safety::Fn<Bad...>` instantiation fires the class-body
// `static_assert(ValidComposition<Fn>)`.  But when a downstream
// metaprogram wants to ask "is rule R013 satisfied for this
// particular Fn?" without computing the whole composition,
// `fixy::rule::L004_OK<F>` is the cheapest, most legible cite.  The
// 20 per-rule concepts are also useful for negative-compile fixtures
// that test individual rule firings.
//
// ── Substrate location ────────────────────────────────────────────
//
//   safety::fn::          ValidComposition, CollisionRules
//   safety::fn::collision::  RuleCode, rule_code_of[_v],
//                            rule_tag_t, rule_bijection_v,
//                            I002_OK..F002_OK, AllRulesOK

// ─── RuleCode enum + bijection helpers ────────────────────────────
using ::crucible::safety::fn::collision::RuleCode;
using ::crucible::safety::fn::collision::rule_code_of;
using ::crucible::safety::fn::collision::rule_code_of_v;
using ::crucible::safety::fn::collision::rule_tag_t;
using ::crucible::safety::fn::collision::rule_bijection_v;

// ─── ValidComposition concept gate + backing trait ────────────────
using ::crucible::safety::fn::ValidComposition;
using ::crucible::safety::fn::CollisionRules;

// ─── 20 per-rule concepts + AllRulesOK aggregate ──────────────────
using ::crucible::safety::fn::collision::I002_OK;
using ::crucible::safety::fn::collision::L002_OK;
using ::crucible::safety::fn::collision::E044_OK;
using ::crucible::safety::fn::collision::I003_OK;
using ::crucible::safety::fn::collision::M012_OK;
using ::crucible::safety::fn::collision::P002_OK;
using ::crucible::safety::fn::collision::I004_OK;
using ::crucible::safety::fn::collision::N002_OK;
using ::crucible::safety::fn::collision::L003_OK;
using ::crucible::safety::fn::collision::M011_OK;
using ::crucible::safety::fn::collision::S010_OK;
using ::crucible::safety::fn::collision::S011_OK;
using ::crucible::safety::fn::collision::L004_OK;
using ::crucible::safety::fn::collision::B001_OK;
using ::crucible::safety::fn::collision::H001_OK;
using ::crucible::safety::fn::collision::H002_OK;
using ::crucible::safety::fn::collision::L005_OK;
using ::crucible::safety::fn::collision::F001_OK;
using ::crucible::safety::fn::collision::H003_OK;
using ::crucible::safety::fn::collision::F002_OK;
using ::crucible::safety::fn::collision::AllRulesOK;

// ─── R001..R020 aliases ───────────────────────────────────────────

using R001 = ::crucible::safety::fn::collision::I002_ClassifiedFailPayload;
using R002 = ::crucible::safety::fn::collision::L002_BorrowAsync;
using R003 = ::crucible::safety::fn::collision::E044_ConstantTimeAsync;
using R004 = ::crucible::safety::fn::collision::I003_ConstantTimeFailOnSecret;
using R005 = ::crucible::safety::fn::collision::M012_MonotonicConcurrentNoAtomic;
using R006 = ::crucible::safety::fn::collision::P002_GhostRuntimeUse;
using R007 = ::crucible::safety::fn::collision::I004_ClassifiedAsyncSession;
using R008 = ::crucible::safety::fn::collision::N002_DecimalOverflowWrap;
using R009 = ::crucible::safety::fn::collision::L003_BorrowUnscopedSpawn;
using R010 = ::crucible::safety::fn::collision::M011_LinearFailNoCleanup;
using R011 = ::crucible::safety::fn::collision::S010_StalenessConstantTime;
using R012 = ::crucible::safety::fn::collision::S011_CapabilityReplay;
using R013 = ::crucible::safety::fn::collision::L004_LinearLifetimeNeedsPermission;
using R014 = ::crucible::safety::fn::collision::B001_BgObservableBoundedResource;
using R015 = ::crucible::safety::fn::collision::H001_HotPathBoundedCost;
using R016 = ::crucible::safety::fn::collision::H002_HotPathWitnessFloor;
using R017 = ::crucible::safety::fn::collision::L005_LinearAliasSameRegionTag;
using R018 = ::crucible::safety::fn::collision::F001_FrameDeclaresAxisCollision;
using R019 = ::crucible::safety::fn::collision::H003_HotPathTerminatingAllocIo;
using R020 = ::crucible::safety::fn::collision::F002_FederationPeerTerminatingBudget;

// ─── Self-test — substrate bijection holds for each alias ─────────
//
// Each rule_code_of_v lookup confirms the substrate considers the
// alias's underlying tag a legitimate rule.  An alias pointing at a
// renamed-or-removed substrate tag fires here at fixy/Rules.h rather
// than at an opaque downstream consumer.

namespace detail::rule_self_test {

using ::crucible::safety::fn::collision::RuleCode;
using ::crucible::safety::fn::collision::rule_code_of_v;

static_assert(rule_code_of_v<R001> == RuleCode::I002);
static_assert(rule_code_of_v<R002> == RuleCode::L002);
static_assert(rule_code_of_v<R003> == RuleCode::E044);
static_assert(rule_code_of_v<R004> == RuleCode::I003);
static_assert(rule_code_of_v<R005> == RuleCode::M012);
static_assert(rule_code_of_v<R006> == RuleCode::P002);
static_assert(rule_code_of_v<R007> == RuleCode::I004);
static_assert(rule_code_of_v<R008> == RuleCode::N002);
static_assert(rule_code_of_v<R009> == RuleCode::L003);
static_assert(rule_code_of_v<R010> == RuleCode::M011);
static_assert(rule_code_of_v<R011> == RuleCode::S010);
static_assert(rule_code_of_v<R012> == RuleCode::S011);
static_assert(rule_code_of_v<R013> == RuleCode::L004);
static_assert(rule_code_of_v<R014> == RuleCode::B001);
static_assert(rule_code_of_v<R015> == RuleCode::H001);
static_assert(rule_code_of_v<R016> == RuleCode::H002);
static_assert(rule_code_of_v<R017> == RuleCode::L005);
static_assert(rule_code_of_v<R018> == RuleCode::F001);
static_assert(rule_code_of_v<R019> == RuleCode::H003);
static_assert(rule_code_of_v<R020> == RuleCode::F002);

}  // namespace detail::rule_self_test

// ── FIXY-U-062: in-header sentinel for the §6.8 aggregator surface ─
//
// Same dual-export discipline as fixy/Bridge.h::self_test +
// fixy/Diag.h::self_test + fixy/Handle.h::self_test.  Drift in the
// substrate's ValidComposition / RuleCode / per-rule concepts
// surfaces here at every consumer's include time, NOT only inside
// test_fixy_rule_helpers.cpp.

namespace u062_self_test {

// 1. RuleCode enum reaches through the alias and the catalog
//    cardinality is exactly 20 + None (sentinel).
static_assert(RuleCode::I002 == ::crucible::safety::fn::collision::RuleCode::I002);
static_assert(RuleCode::F002 == ::crucible::safety::fn::collision::RuleCode::F002);
static_assert(RuleCode::None == ::crucible::safety::fn::collision::RuleCode::None);

// 2. ValidComposition resolves through the alias for the canonical
//    well-formed Fn<int> probe (substrate's own positive witness at
//    safety/Fn.h:572).  This pins both ValidComposition's identity
//    AND that the using-decl correctly imports the concept (not just
//    a name).
static_assert(ValidComposition<::crucible::safety::fn::Fn<int>>,
    "fixy::rule::ValidComposition must accept the Fn<int> positive "
    "probe — substrate identity for the §6.8 gate.");

// 3. CollisionRules<F>::valid reaches through the alias and agrees
//    with ValidComposition (the concept reads `::valid` from the
//    trait — bypassing the concept lets us check the trait alone).
static_assert(CollisionRules<::crucible::safety::fn::Fn<int>>::valid,
    "fixy::rule::CollisionRules<F>::valid must alias substrate trait");

// 4. AllRulesOK aggregate evaluates true on the positive probe — the
//    conjunction of 20 per-rule concepts equals ValidComposition.
static_assert(AllRulesOK<::crucible::safety::fn::Fn<int>>,
    "fixy::rule::AllRulesOK must hold on the canonical probe");

// 5. bijection — rule_code_of(rule_tag_t<R>) == R, witnessed via the
//    alias.  Spot-check three rules from different catalog regions
//    (Phase-0, Phase-B-early, Phase-B-late).
static_assert(rule_bijection_v<RuleCode::I002>);
static_assert(rule_bijection_v<RuleCode::L004>);
static_assert(rule_bijection_v<RuleCode::F002>);

// 6. rule_tag_t alias resolves to the substrate's tag class.
static_assert(std::is_same_v<
    rule_tag_t<RuleCode::I002>,
    ::crucible::safety::fn::collision::I002_ClassifiedFailPayload>,
    "fixy::rule::rule_tag_t alias must resolve to substrate tag");

// 7. Per-rule concepts I002_OK..F002_OK reach through the alias on
//    the positive probe.  All 20 evaluate true for Fn<int>.
static_assert(I002_OK<::crucible::safety::fn::Fn<int>>);
static_assert(L002_OK<::crucible::safety::fn::Fn<int>>);
static_assert(E044_OK<::crucible::safety::fn::Fn<int>>);
static_assert(I003_OK<::crucible::safety::fn::Fn<int>>);
static_assert(M012_OK<::crucible::safety::fn::Fn<int>>);
static_assert(P002_OK<::crucible::safety::fn::Fn<int>>);
static_assert(I004_OK<::crucible::safety::fn::Fn<int>>);
static_assert(N002_OK<::crucible::safety::fn::Fn<int>>);
static_assert(L003_OK<::crucible::safety::fn::Fn<int>>);
static_assert(M011_OK<::crucible::safety::fn::Fn<int>>);
static_assert(S010_OK<::crucible::safety::fn::Fn<int>>);
static_assert(S011_OK<::crucible::safety::fn::Fn<int>>);
static_assert(L004_OK<::crucible::safety::fn::Fn<int>>);
static_assert(B001_OK<::crucible::safety::fn::Fn<int>>);
static_assert(H001_OK<::crucible::safety::fn::Fn<int>>);
static_assert(H002_OK<::crucible::safety::fn::Fn<int>>);
static_assert(L005_OK<::crucible::safety::fn::Fn<int>>);
static_assert(F001_OK<::crucible::safety::fn::Fn<int>>);
static_assert(H003_OK<::crucible::safety::fn::Fn<int>>);
static_assert(F002_OK<::crucible::safety::fn::Fn<int>>);

// 8. Cardinality witness — count of items surfaced by U-062 (in
//    addition to the 20 R001..R020 tag aliases already shipped).
//    Drift in either direction (substrate adds a new rule, or a
//    using-decl above is removed) must update this number.
//
//      RuleCode + rule_code_of + rule_code_of_v + rule_tag_t +
//        rule_bijection_v                                   = 5
//      ValidComposition + CollisionRules                    = 2
//      I002_OK..F002_OK (20 per-rule concepts)              = 20
//      AllRulesOK                                            = 1
//                                                          ----
//                                                            28
constexpr int u062_surface_cardinality = 28;
static_assert(u062_surface_cardinality == 28,
    "fixy::rule:: U-062 surface cardinality drifted — update Rules.h "
    "using-decls AND this sentinel in lockstep.");

}  // namespace u062_self_test

}  // namespace crucible::fixy::rule
