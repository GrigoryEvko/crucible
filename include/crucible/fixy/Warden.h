#pragma once

// ── crucible::fixy::warden — Warden minters under fixy:: ──────────
//
// FIXY-U-120.  Re-exports the four warden-tree §XXI mint factories
// under `fixy::warden::` so callers who include only the fixy umbrella
// do not have to descend into the warden/ tree to mint the cold-init
// enforcement primitives.
//
// Per CLAUDE.md §XXI Universal Mint Pattern, each re-export preserves
// the substrate's CtxFitsXMint concept gate (Init-row admission), the
// `[[nodiscard]] constexpr noexcept` qualifiers (allocation-free
// across all four — the policy state lives in Pinned singletons or
// trivially-copyable structs minted in place), and the ctx-bound
// authorization shape (`Ctx const&` first parameter, exec-row contains
// Effect::Init).
//
// ── Substrate consumed ─────────────────────────────────────────────
//
//   warden::mint_hardening(ctx, policy)                       — Hardening.h
//   warden::mint_deadline_watchdog(ctx, senses, policy)       — DeadlineWatchdog.h
//   warden::mint_hot_region_registry_handle(ctx)              — Registry.h
//   warden::mint_quarantine_policy<MaxCogs, MaxEvents>(ctx,..)— Quarantine.h
//
// ── Why every mint is Init-row gated ──────────────────────────────
//
// All four surfaces engage process-wide state mutations that belong
// to the startup-only Init row:
//
//   - `mint_hardening` issues sched_setaffinity / sched_setattr /
//     mlock2 / madvise(MADV_HUGEPAGE|MADV_COLLAPSE) / prctl syscalls
//     against the calling thread / process address space.
//   - `mint_deadline_watchdog` baselines a rolling-window observer
//     against Senses + steady_clock; hot foreground / bg-drain
//     contexts must not stand up a fresh watchdog (they observe one
//     minted at Init time by the Keeper / bench harness).
//   - `mint_hot_region_registry_handle` returns the 1-byte
//     authorization token for the Pinned process-wide registry — the
//     act of minting is the proof of Init-tier authority, even
//     though the underlying state already exists.
//   - `mint_quarantine_policy` constructs a fresh per-fleet
//     quarantine watcher with its own ring buffer + per-Cog state;
//     fleet-shape decisions are an Init-row concern.
//
// Hot foreground (`HotFgCtx`) and background drain (`BgDrainCtx`)
// contexts are statically rejected at each mint's requires-clause.
// The substrate ships the rejection static_asserts (witnessed in
// `warden/{Hardening,DeadlineWatchdog,Registry,Quarantine}.h`); this
// header relays them through the fixy:: layer so the same negative
// reach holds through the umbrella.
//
// ── Axiom coverage ─────────────────────────────────────────────────
//
//   InitSafe — re-exports introduce no new state path; the
//              substrate's NSDMI + Pinned singletons handle init.
//   TypeSafe — using-declarations preserve the substrate's concept
//              gates (CtxFitsHardeningMint, CtxFitsDeadlineWatchdogMint,
//              CtxFitsHotRegionRegistryMint, CtxFitsQuarantineMint).
//   NullSafe — DeadlineWatchdog::ctor takes `const Senses*` and the
//              mint forwards it directly; substrate enforces non-null
//              via its own contract.  Other three mints take no
//              pointer parameters.
//   MemSafe  — zero heap.  Every mint is `constexpr` and constructs
//              in place; AppliedPolicy / DeadlineWatchdog /
//              HotRegionRegistryHandle / QuarantinePolicy each fit
//              in their declared sizeof slots.
//   BorrowSafe — no shared state.  HotRegionRegistryHandle is a
//                proof-token; the underlying registry's atomics
//                live in the Pinned singleton.
//   ThreadSafe — only `mint_hot_region_registry_handle` returns a
//                handle into shared atomic state; the underlying
//                discipline (release-store on registration,
//                acquire-load on probe) is owned by Registry.h.
//   LeakSafe — every mint returns a value type with trivial dtor;
//              no resource ownership crosses through the using-decl.
//   DetSafe  — Init-tier mints; not on the deterministic-replay
//              path.  Watchdog observations are advisory.
//
// ── Allocation discipline (zero-heap across all four mints) ───────
//
// Every warden mint is pure-construct: the body either returns a
// value-typed struct via NRVO (AppliedPolicy, QuarantinePolicy) or
// constructs a trivially-copyable token (HotRegionRegistryHandle).
// DeadlineWatchdog's substrate ctor reads Senses fields + steady_clock
// + the Policy baseline into a stack-allocated struct; the mint is
// `constexpr` and `noexcept`, no heap path can fire.
//
// This is unlike `fixy::bridge::mint_persisted_session` (heap via
// `std::unique_ptr<SessionPersistenceState>`); the warden surface is
// fully zero-alloc and the fixy:: re-exports preserve that.

#include <crucible/warden/DeadlineWatchdog.h>
#include <crucible/warden/Hardening.h>
#include <crucible/warden/Quarantine.h>
#include <crucible/warden/Registry.h>

#include <type_traits>  // sentinel block

namespace crucible::fixy::warden {

// ═════════════════════════════════════════════════════════════════════
// ── Hardening — Linux syscall policy applicator ────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// `mint_hardening(ctx, policy)` returns an `AppliedPolicy` RAII guard
// that captures the pre-mutation state for revert-on-drop semantics.
// Concept gate `CtxFitsHardeningMint<Ctx>` requires Init in row.

using ::crucible::warden::mint_hardening;
using ::crucible::warden::CtxFitsHardeningMint;
using ::crucible::warden::AppliedPolicy;
using ::crucible::warden::Policy;
using ::crucible::warden::Hardening;

// ═════════════════════════════════════════════════════════════════════
// ── Deadline watchdog — foreground-stall detection observer ────────
// ═════════════════════════════════════════════════════════════════════
//
// `mint_deadline_watchdog(ctx, senses, policy)` baselines a rolling-
// window stall observer against the perf::Senses telemetry surface.
// Concept gate `CtxFitsDeadlineWatchdogMint<Ctx>` requires Init.

using ::crucible::warden::mint_deadline_watchdog;
using ::crucible::warden::CtxFitsDeadlineWatchdogMint;
using ::crucible::warden::DeadlineWatchdog;

// ═════════════════════════════════════════════════════════════════════
// ── Hot-region registry — process-wide mlock/madvise registry ──────
// ═════════════════════════════════════════════════════════════════════
//
// `mint_hot_region_registry_handle(ctx)` returns a 1-byte token whose
// existence proves Init-tier authority; the underlying registry state
// lives in a Pinned process-wide singleton.  Concept gate
// `CtxFitsHotRegionRegistryMint<Ctx>` requires Init.

using ::crucible::warden::mint_hot_region_registry_handle;
using ::crucible::warden::CtxFitsHotRegionRegistryMint;
using ::crucible::warden::HotRegionRegistryHandle;

// ═════════════════════════════════════════════════════════════════════
// ── Quarantine policy — degraded-Cog lifecycle bound ───────────────
// ═════════════════════════════════════════════════════════════════════
//
// `mint_quarantine_policy<Ctx, MaxCogs, MaxEvents>(ctx, config)`
// returns a fresh `QuarantinePolicy<MaxCogs, MaxEvents>` watcher.
// Note: this is a function-template mint with non-type parameters;
// using-decl re-exports the name into fixy::warden:: and concrete
// instantiations resolve to the substrate template.
//
// The companion concept variants `CtxFitsQuarantineRecord` and
// `CtxFitsQuarantineOverride` admit BgDrainCtx / TestRunnerCtx for
// record-only and override-only callers (see Quarantine.h substrate
// static_asserts); they are re-exported here for completeness so
// the fixy:: layer surfaces the full quarantine-mint concept family.

using ::crucible::warden::mint_quarantine_policy;
using ::crucible::warden::CtxFitsQuarantineMint;
using ::crucible::warden::CtxFitsQuarantineRecord;
using ::crucible::warden::CtxFitsQuarantineOverride;
using ::crucible::warden::QuarantinePolicy;
using ::crucible::warden::QuarantineConfig;
using ::crucible::warden::QuarantineTransition;
using ::crucible::warden::QuarantineSnapshot;
using ::crucible::warden::QuarantineEvent;

}  // namespace crucible::fixy::warden

// ─── Dual-export sentinel — FIXY-U-120 ─────────────────────────────
//
// Header-internal identity sentinels for every warden surface item.
// Each alias resolves to its substrate type, not a shadowed local.
// Same recipe as fixy/Bridge.h::self_test + fixy/Cap.h::self_test —
// drift surfaces here at every consumer's include time, NOT only in
// test_fixy_warden.cpp.  Cardinality witness at the tail catches a
// future contributor adding (or removing) a warden mint without
// updating both the using-decl block AND this sentinel.

namespace crucible::fixy::warden::self_test {

// Type-identity witnesses.  Each `static_assert(std::is_same_v<...>)`
// proves the fixy:: alias resolves to the same type as the substrate;
// a future regression that introduces a shadowed local declaration
// or accidentally rewrites the using-decl to import a different
// symbol would red the build at this header's first include.

static_assert(std::is_same_v<
    ::crucible::fixy::warden::AppliedPolicy,
    ::crucible::warden::AppliedPolicy>,
    "fixy::warden::AppliedPolicy must alias warden::AppliedPolicy.");

static_assert(std::is_same_v<
    ::crucible::fixy::warden::Policy,
    ::crucible::warden::Policy>,
    "fixy::warden::Policy must alias warden::Policy.");

static_assert(std::is_same_v<
    ::crucible::fixy::warden::DeadlineWatchdog,
    ::crucible::warden::DeadlineWatchdog>,
    "fixy::warden::DeadlineWatchdog must alias warden::DeadlineWatchdog.");

static_assert(std::is_same_v<
    ::crucible::fixy::warden::HotRegionRegistryHandle,
    ::crucible::warden::HotRegionRegistryHandle>,
    "fixy::warden::HotRegionRegistryHandle must alias substrate.");

static_assert(std::is_same_v<
    ::crucible::fixy::warden::QuarantineConfig,
    ::crucible::warden::QuarantineConfig>,
    "fixy::warden::QuarantineConfig must alias substrate.");

static_assert(std::is_same_v<
    ::crucible::fixy::warden::QuarantineEvent,
    ::crucible::warden::QuarantineEvent>,
    "fixy::warden::QuarantineEvent must alias substrate.");

// Concept-resolution witnesses.  Each concept name is satisfied by
// `ColdInitCtx` (which carries Init in its effect row); the substrate
// ships the same static_asserts at the mint definition site.  This
// duplication is deliberate — surfaces drift through the fixy:: layer
// at every consumer's include time.

static_assert(::crucible::fixy::warden::CtxFitsHardeningMint<
    ::crucible::effects::ColdInitCtx>,
    "fixy::warden::CtxFitsHardeningMint must admit ColdInitCtx.");

static_assert(::crucible::fixy::warden::CtxFitsDeadlineWatchdogMint<
    ::crucible::effects::ColdInitCtx>,
    "fixy::warden::CtxFitsDeadlineWatchdogMint must admit ColdInitCtx.");

static_assert(::crucible::fixy::warden::CtxFitsHotRegionRegistryMint<
    ::crucible::effects::ColdInitCtx>,
    "fixy::warden::CtxFitsHotRegionRegistryMint must admit ColdInitCtx.");

static_assert(::crucible::fixy::warden::CtxFitsQuarantineMint<
    ::crucible::effects::ColdInitCtx>,
    "fixy::warden::CtxFitsQuarantineMint must admit ColdInitCtx.");

// Negative-reach witness.  `BgDrainCtx` carries `Bg, Alloc` but NOT
// `Init`; every Init-only warden mint must reject it.  The substrate
// ships these `!CtxFits...<BgDrainCtx>` asserts at the mint site;
// re-asserting through the fixy:: surface witnesses the gate is not
// silently relaxed by the using-decl.

static_assert(!::crucible::fixy::warden::CtxFitsHardeningMint<
    ::crucible::effects::BgDrainCtx>,
    "fixy::warden::CtxFitsHardeningMint must reject BgDrainCtx.");

static_assert(!::crucible::fixy::warden::CtxFitsDeadlineWatchdogMint<
    ::crucible::effects::BgDrainCtx>,
    "fixy::warden::CtxFitsDeadlineWatchdogMint must reject BgDrainCtx.");

static_assert(!::crucible::fixy::warden::CtxFitsHotRegionRegistryMint<
    ::crucible::effects::BgDrainCtx>,
    "fixy::warden::CtxFitsHotRegionRegistryMint must reject BgDrainCtx.");

static_assert(!::crucible::fixy::warden::CtxFitsQuarantineMint<
    ::crucible::effects::BgDrainCtx>,
    "fixy::warden::CtxFitsQuarantineMint must reject BgDrainCtx.");

// Cardinality witness.  Four mint factories live in `warden/`.  If a
// future contributor adds a fifth (or removes one of the current
// four), this constant drifts and the matching test_fixy_warden.cpp
// `static_assert(warden_mint_cardinality == 4)` reds the build,
// prompting the auditor to either update both sites in lockstep or
// document the drift in the inventory regeneration step.

inline constexpr int warden_mint_cardinality = 4;

}  // namespace crucible::fixy::warden::self_test

// ─── Runtime smoke test ────────────────────────────────────────────
//
// Per FIXY-U-103 discipline (every fixy/ header ships a runtime
// smoke block).  Smoke for warden mints is type-level: we cannot
// actually invoke `mint_hardening` from a smoke routine without
// performing real Linux syscalls, so the smoke verifies the
// re-export name resolution at runtime context (instantiation
// already-exercised at consteval above; this block ensures the
// header is reachable via runtime-call paths too).

namespace crucible::fixy::warden {

inline void runtime_smoke_test() noexcept {
    // Witness that the concept aliases instantiate at runtime context.
    constexpr bool admits_cold = CtxFitsHardeningMint<
        ::crucible::effects::ColdInitCtx>;
    constexpr bool rejects_bg = !CtxFitsHardeningMint<
        ::crucible::effects::BgDrainCtx>;
    (void)admits_cold;
    (void)rejects_bg;
}

}  // namespace crucible::fixy::warden
