#pragma once

// ── crucible::fixy — Rules.h (FIXY-B4) ─────────────────────────────────
//
// Re-export of the substrate's 12 §6.8 collision rule tags under the
// `fixy::rule::*` namespace.  When a `fixy::fn<Type, Grants...>`
// instantiation reaches the underlying `safety::fn::Fn<...>` body,
// the substrate's `ValidComposition<Fn>` concept fires the
// appropriate rule's diagnostic.  Re-exporting under fixy:: lets
// fixy-side callers reference the rule tags without crossing the
// `safety::fn::collision::` boundary.
//
// ── The 12 rules (fixy.md §6.8 Table A) ──────────────────────────────
//
//   I002 — classified value flows through Fail(E) without secret payload.
//   L002 — borrow combined with async suspension.
//   E044 — constant-time region combined with async scheduling.
//   I003 — constant-time function fails on secret-dependent condition.
//   M012 — monotonic mutation in concurrent context without atomic.
//   P002 — ghost data used by runtime code.
//   I004 — classified async session without CT discipline.
//   N002 — exact decimal type combined with wrap overflow.
//   L003 — borrow combined with unscoped spawn.
//   M011 — linear resource live across Fail without cleanup.
//   S010 — non-fresh staleness combined with constant-time.
//   S011 — ephemeral capability used in replay-required code.
//
// ── Reachability through `fixy::fn` (FIXY-B4 audit) ──────────────────
//
// Substrate's collision detectors live in `safety/CollisionCatalog.h`
// and check both AXIS values (Usage/Mutation/Overflow/Repr/etc., set
// directly via grant tags) AND MARKER traits (`marks_async`,
// `marks_ct`, `marks_fail`, `marks_runtime_ghost_use`, ...) that the
// substrate primary-templates to false_type.  Marker propagation from
// fixy grants to substrate marker traits requires partial
// specialization on `marks_X<safety::fn::Fn<...>>` — substrate-side
// scaffold; not in fixy slice.
//
//   Rule | Reachable via grants today | Gap
//   -----+----------------------------+----------------------------
//   I002 | NO  — needs marks_fail + marks_fail_error_secret
//   L002 | NO  — needs marks_async (grant::borrow gives borrow_capture)
//   E044 | NO  — needs marks_ct + marks_async
//   I003 | NO  — needs marks_ct + marks_fail + marks_fail_on_secret
//   M012 | YES — grant::monotonic_advance + grant::with<Bg>; SHIPPED
//   P002 | NO  — grant::ghost gives Usage=Ghost; needs marks_runtime_ghost_use
//   I004 | NO  — needs marks_async on classified session path
//   N002 | YES — grant::overflow_wrap + user-defined exact_decimal type
//   L003 | NO  — needs marks_unscoped_spawn
//   M011 | NO  — needs marks_fail + marks_linear_uncleaned_fail
//   S010 | NO  — needs marks_ct
//   S011 | NO  — needs marks_replay_required (capability replay tracking)
//
// Of the 12 rules, **2 are reachable today** purely through grant tags
// (M012 with a fixture; N002 needs a user-side exact_decimal type so
// the fixture lives test-side).  The remaining 10 await substrate
// marker-propagation work; their fixtures are documented gaps, not
// missing tests.
//
// ── Axiom coverage ──────────────────────────────────────────────────
//
//   TypeSafe — substrate tag identity preserved via `using` aliases.
//   DetSafe  — bit-identical re-export across compiles.
//
// ── References ──────────────────────────────────────────────────────
//
//   misc/16_05_2026_fixy.md §4 Phase B
//   misc/fixy.md §24.2 collision rule table
//   safety/CollisionCatalog.h — substrate-side rule tags

#include <crucible/safety/CollisionCatalog.h>

#include <type_traits>

namespace crucible::fixy::rule {

namespace _collision = ::crucible::safety::fn::collision;

// ─── 12-rule re-export ─────────────────────────────────────────────
using I002 = _collision::I002_ClassifiedFailPayload;
using L002 = _collision::L002_BorrowAsync;
using E044 = _collision::E044_ConstantTimeAsync;
using I003 = _collision::I003_ConstantTimeFailOnSecret;
using M012 = _collision::M012_MonotonicConcurrentNoAtomic;
using P002 = _collision::P002_GhostRuntimeUse;
using I004 = _collision::I004_ClassifiedAsyncSession;
using N002 = _collision::N002_DecimalOverflowWrap;
using L003 = _collision::L003_BorrowUnscopedSpawn;
using M011 = _collision::M011_LinearFailNoCleanup;
using S010 = _collision::S010_StalenessConstantTime;
using S011 = _collision::S011_CapabilityReplay;

// ─── RuleCode enum re-export ───────────────────────────────────────
using RuleCode = _collision::RuleCode;

// ─── Catalog tuple re-export ───────────────────────────────────────
using Catalog = _collision::Catalog;

static_assert(std::tuple_size_v<Catalog> == 12,
    "fixy::rule::Catalog must mirror substrate's 12-rule catalog.");

// ─── Bijection self-tests ──────────────────────────────────────────
//
// Each fixy::rule::X is the same TYPE as the substrate's
// corresponding collision tag.  std::is_same_v pins the aliasing.

static_assert(std::is_same_v<I002, _collision::I002_ClassifiedFailPayload>);
static_assert(std::is_same_v<L002, _collision::L002_BorrowAsync>);
static_assert(std::is_same_v<E044, _collision::E044_ConstantTimeAsync>);
static_assert(std::is_same_v<I003, _collision::I003_ConstantTimeFailOnSecret>);
static_assert(std::is_same_v<M012, _collision::M012_MonotonicConcurrentNoAtomic>);
static_assert(std::is_same_v<P002, _collision::P002_GhostRuntimeUse>);
static_assert(std::is_same_v<I004, _collision::I004_ClassifiedAsyncSession>);
static_assert(std::is_same_v<N002, _collision::N002_DecimalOverflowWrap>);
static_assert(std::is_same_v<L003, _collision::L003_BorrowUnscopedSpawn>);
static_assert(std::is_same_v<M011, _collision::M011_LinearFailNoCleanup>);
static_assert(std::is_same_v<S010, _collision::S010_StalenessConstantTime>);
static_assert(std::is_same_v<S011, _collision::S011_CapabilityReplay>);

}  // namespace crucible::fixy::rule
