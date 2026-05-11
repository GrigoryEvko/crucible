// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #2 of 2 for mimic::CogMimic (GAPS-188 #1213).
//
// Premise: CtxFitsCogMimic<Ctx, K> conjuncts on six gates including
//
//   decide::row_subset<Row<Effect::Init>, Ctx::row_type>()
//     OR decide::row_subset<Row<Effect::Bg>, Ctx::row_type>()
//
// — minting a CogMimic instance is permitted only in calibration-time
// setup contexts (Effect::Init) or background recalibration contexts
// during fleet operation (Effect::Bg).  Foreground hot-path contexts
// (Effect::row_pure / row_empty) and Test contexts must NOT mint
// CogMimic instances; doing so would either (a) introduce calibration
// work on the latency-critical foreground path, or (b) bind a Test-
// only carrier into the production topology graph.
//
// Why this is the load-bearing soundness gate:
//
// Without the row-membership conjunct, a Test fixture that spawns a
// Test-context worker and accidentally calls
//
//   mint_cog_mimic<Gpu>(test_ctx, identity, caps, opcodes)
//
// produces a CogMimic<Gpu> bound to the test fixture's identity arena.
// When that test fixture's arena unwinds (test teardown), the
// downstream code holding a CogMimic<Gpu>::identity pointer dereferences
// dangling memory.  Catching the misuse at the row-membership conjunct
// at template substitution time is structurally stronger than a
// runtime "test ctx leaked into production code" assertion.
//
// Companion fixture: neg_cog_mimic_non_substrate.cpp
//   * That one tests rejection at the SUBSTRATE-FAMILY gate —
//     IsMimicSubstrate refuses Power / Sensor / Container kinds at the
//     requires-clause.  Distinct mismatch class (structural family-
//     membership failure: "this Cog has no Mimic instance to mint").
//   * This one tests rejection at the CTX-ROW gate — the disjunctive
//     row_subset conjunct refuses contexts whose effect row
//     carries neither Init nor Bg.  Distinct mismatch class (effect-
//     row admission failure on a TEMPORALLY-INAPPROPRIATE context).
//
// Per HS14, ≥2 negative-compile fixtures per new soundness gate, each
// demonstrating a distinct mismatch class.
//
// Expected diagnostic: "constraint not satisfied" / "constraints not
// satisfied" / "CogMimic" / "CtxFitsCogMimic" / "row_subset" /
// "Effect::Init" / "Effect::Bg" / "Test" / "GAPS-188" pointing at the
// call site below.

#include <crucible/cog/CogIdentity.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/effects/EffectRow.h>
#include <crucible/effects/ExecCtx.h>
#include <crucible/mimic/CogMimic.h>

namespace cog     = crucible::cog;
namespace mimic   = crucible::mimic;
namespace effects = crucible::effects;

// Mock of an unintended call from a Test-context worker that tries to
// mint a CogMimic instance.  The CogKind admits at the substrate
// gate (Gpu is family=Compute, fully admitted), so the rejection
// surfaces specifically at the row-membership conjunct.
template <effects::IsExecCtx Ctx, cog::CogKind K>
    requires mimic::CtxFitsCogMimic<Ctx, K>
constexpr int prepare_cog_mimic_for_test() noexcept { return 1; }

// TestCtx — Effect::Test row.  Neither Init nor Bg appears.  The
// disjunctive row_subset predicate in CtxFitsCogMimic refuses
// substitution.
using TestCtx = effects::ExecCtx<
    effects::Test,
    effects::ctx_numa::Any,
    effects::ctx_alloc::Stack,
    effects::ctx_heat::Cold,
    effects::ctx_resid::DRAM,
    effects::Row<effects::Effect::Test>,
    effects::ctx_workload::Unspecified>;

static_assert(prepare_cog_mimic_for_test<TestCtx, cog::CogKind::Gpu>() == 1,
    "GAPS-188: mimic::CtxFitsCogMimic concept MUST refuse contexts "
    "whose effect row carries neither Effect::Init nor Effect::Bg.  "
    "If this static_assert ever evaluates, a Test-context worker (or "
    "a foreground hot-path call site that accidentally reached the "
    "minting API) would bind a CogMimic instance to a transient "
    "test/foreground arena.  When the arena unwinds, downstream code "
    "holding the CogMimic::identity pointer would dereference dangling "
    "memory.  The row-membership gate refuses the misuse at template "
    "substitution time — calibration-time (Init) or background-"
    "recalibration (Bg) are the only contexts in which a CogMimic "
    "instance is structurally legitimate.");

int main() { return 0; }
