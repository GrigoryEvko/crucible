#pragma once

// ── crucible::fixy::rule — R001..R020 alias projection ─────────────
//
// Phase B of misc/16_05_2026_fixy.md §4.  Re-exports the 20 §6.8
// collision rule tags from safety/CollisionCatalog.h under the
// grep-discoverable `fixy::rule::R<NNN>` namespace.
//
// Phase B convention: every production binding that wants to reference
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
// trait, no new diagnostic.  If a Phase B reviewer adds a rule tag
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

}  // namespace crucible::fixy::rule
