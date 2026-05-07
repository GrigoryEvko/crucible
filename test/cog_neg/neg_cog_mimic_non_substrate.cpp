// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #1 of 2 for mimic::CogMimic (GAPS-188 #1213).
//
// Premise: CogMimic<K> binds the abstract per-Cog Mimic instance triple
// (CogIdentity*, calibrated TargetCaps, OpcodeLatencyTable) for any
// substrate Cog kind — Compute (Gpu / CpuCore / CpuSocket), Network
// (NicPort / NvSwitch), Memory (DramChannel), or Bus (future
// PcieLaneGroup).  The requires-clause conjuncts on
// `IsMimicSubstrate<K>` (family ∈ {Compute, Network, Memory, Bus}) AND
// `HasCaps<K>` AND `HasOpcodeTable<K>` AND
// `has_cog_mimic_projection_v<K>`.
//
// Non-substrate kinds — Power family (PsuRail / RackPsu), Sensor
// family (BmcSensor), Container family (Server / Rack / Row / Hall /
// Datacenter) — REFUSE the IsMimicSubstrate conjunct.  These Cogs do
// not host a per-Cog Mimic instance: PSU rails are non-schedulable
// power-distribution carriers; BMC sensors are pure observability
// sources; L2+ Container Cogs are enclosures whose contained L0/L1
// substrate Cogs each have their own Mimic instance.
//
// Why this is the load-bearing soundness gate:
//
// Without the IsMimicSubstrate gate, a future GAPS-196 calibrate
// pass (or a GAPS-810 partition optimiser invoking mint_cog_mimic
// for every Cog in the topology graph) could accidentally call
//
//   mint_cog_mimic<CogKind::PsuRail>(ctx, psu_identity, ?, ?)
//
// where `?` is `cog::caps_for_t<PsuRail>` (which doesn't exist —
// no caps schema for power rails) and `cog::OpcodeLatencyTable<
// PsuRail>` (which doesn't exist either — no opcode catalog for
// power-distribution carriers).  The resulting compile error would
// be a deep substitution failure inside the Tagged<...> field
// constructor, far from the call site.
//
// The IsMimicSubstrate gate refuses the structural misuse at the
// call site itself, naming the rejected family (Power) in the
// diagnostic.  Even if a future caps_for<PsuRail> ships by accident
// (review oversight), the IsMimicSubstrate intent gate continues to
// refuse — defense in depth: family-level intent + caps/opcodes
// operational shipped check.
//
// Companion fixture: neg_cog_mimic_ctx_row_missing.cpp
//   * That one tests rejection at the CTX-ROW gate — minting a
//     CogMimic from a Test ctx (whose effect row carries neither
//     Effect::Init nor Effect::Bg) is structurally a misuse.
//     Distinct mismatch class (effect-row admission failure on a
//     TEMPORALLY-INAPPROPRIATE context).
//   * This one tests rejection at the SUBSTRATE-FAMILY gate —
//     IsMimicSubstrate refuses Power / Sensor / Container kinds at
//     the requires-clause.  Distinct mismatch class (structural
//     family-membership failure: "this Cog has no Mimic instance to
//     mint").
//
// Per HS14, ≥2 negative-compile fixtures per new soundness gate, each
// demonstrating a distinct mismatch class.
//
// Expected diagnostic: "constraint not satisfied" / "constraints not
// satisfied" / "CogMimic" / "IsMimicSubstrate" / "PsuRail" /
// "GAPS-188" pointing at the call site below.

#include <crucible/cog/CogIdentity.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/effects/EffectRow.h>
#include <crucible/effects/ExecCtx.h>
#include <crucible/mimic/CogMimic.h>

namespace cog     = crucible::cog;
namespace mimic   = crucible::mimic;
namespace effects = crucible::effects;

// Mock of the future GAPS-196 calibrate-pass / GAPS-810 partition-
// optimiser shape: a function templated on a CogKind atom, constrained
// on CtxFitsCogMimic.  Calling with a non-substrate K (Power family)
// fails the embedded IsMimicSubstrate conjunct because cog_family_v<
// PsuRail> == CogFamily::Power, which is not one of the four substrate
// families admitted by IsMimicSubstrate.
template <cog::CogKind K, effects::IsExecCtx Ctx>
    requires mimic::CtxFitsCogMimic<Ctx, K>
constexpr int allocate_cog_mimic_slot() noexcept { return 1; }

// InitCtx — a fitting calibration-time minting context for any
// substrate Cog.  Used here as the Ctx argument so the rejection
// surfaces specifically at the CogKind conjunct (not at the ctx-row
// conjunct, which is what fixture #2 exercises).
using InitCtx = effects::ExecCtx<
    effects::Init,
    effects::ctx_numa::Any,
    effects::ctx_alloc::Unbound,
    effects::ctx_heat::Cold,
    effects::ctx_resid::DRAM,
    effects::Row<effects::Effect::Init>,
    effects::ctx_workload::Unspecified>;

// CogKind::PsuRail is family=Power.  IsMimicSubstrate<PsuRail> is
// false → CogMimic<PsuRail>'s requires-clause refuses substitution.
// Build fails here at the static_assert call site, naming the broken
// concept conjunct.
static_assert(allocate_cog_mimic_slot<cog::CogKind::PsuRail, InitCtx>() == 1,
    "GAPS-188: mimic::CogMimic concept MUST refuse non-substrate "
    "CogKind atoms (Power / Sensor / Container families) at template "
    "substitution.  If this static_assert ever evaluates, a future "
    "GAPS-196 calibrate pass or GAPS-810 partition optimiser would "
    "silently accept PsuRail as a Mimic-instance target and produce "
    "either (a) a deep substitution failure inside Tagged<caps_for_t<"
    "PsuRail>, source::Calibrated> (because caps_for<PsuRail> is not "
    "specialised), or (b) — if a future caps_for<PsuRail> ships "
    "accidentally — a Mimic stub bound to a power-distribution Cog "
    "that has no compute / network / memory operations to emit code "
    "for.  The IsMimicSubstrate gate refuses the structural misuse "
    "at the call site, regardless of whether caps_for has been "
    "shipped — defense in depth via family-level intent gating.");

int main() { return 0; }
