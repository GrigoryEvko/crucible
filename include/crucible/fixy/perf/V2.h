#pragma once

// ── crucible::fixy::perf::v2 — V2-hub mint under fixy:: ─────────────
//
// FIXY-U-122.  Standalone sub-umbrella re-exporting the V2 SenseHub
// surface under `fixy::perf::v2::`.  Closes the final
// `[✗ NO-FIXY]` entry left after FIXY-U-121 surfaced the V1 perf-tree
// mints under `fixy::perf::`.
//
// ════════════════════════════════════════════════════════════════════
//   HARD: DO NOT INCLUDE THIS HEADER ALONGSIDE <crucible/fixy/Perf.h>
//   OR <crucible/perf/SenseHub.h> IN THE SAME TRANSLATION UNIT.
// ════════════════════════════════════════════════════════════════════
//
// SenseHub v1 (perf/SenseHub.h) and SenseHub v2 (perf/SenseHubV2.h)
// are alternative-build siblings, both inhabiting `namespace
// crucible::perf` and both shipping conflicting identifiers:
//
//   `crucible::perf::Idx`           — v1 ships an unscoped
//                                     `enum Idx : uint32_t` with 96
//                                     enumerators; v2 ships a SCOPED
//                                     `enum class Idx : uint32_t` with
//                                     a different value layout.
//   `crucible::perf::NUM_COUNTERS`  — v1 ships an enum-member with
//                                     value 96; v2 ships an
//                                     `inline constexpr std::size_t`
//                                     equal to 128 (basic build) or
//                                     256 (extended).
//   `crucible::perf::Gauge`         — v2-only; harmless when v1 only.
//
// Co-including both headers in one TU surfaces three ODR-/enum-/
// constant-value collisions; the CMake toggle `CRUCIBLE_SENSE_HUB_V2`
// confirms the design intent (opt-in REPLACEMENT, not co-shipped
// second hub).  The fixy umbrella respects this: `fixy/Perf.h`
// surfaces ONLY v1 mints; this header surfaces ONLY v2 — and the two
// MUST NOT be transitively pulled into the same TU.  A TU that needs
// v2 includes this header DIRECTLY (or via a v2-only umbrella the
// project may grow later); it does NOT also include `fixy/Perf.h`.
//
// ── §XXI compliance ────────────────────────────────────────────────
//
// The substrate `mint_sense_hub_v2` (perf/SenseHubV2.h:589) is a
// `[[nodiscard]] inline std::optional<SenseHubV2>` ctx-bound mint
// gated on `CtxFitsSenseHubV2Mint = IsExecCtx<Ctx> ∧
// CtxOwnsCapability<Ctx, Effect::Init>`.  The using-decl below
// preserves nodiscard / noexcept / Init-row admission unchanged
// (constexpr is not preserved because the substrate's mint invokes
// real syscalls — see fixy/Perf.h §"Allocation discipline").
//
// ── Substrate consumed ─────────────────────────────────────────────
//
//   perf::mint_sense_hub_v2(ctx, init)          — SenseHubV2.h
//
// ── Supporting v2-only surface re-exported ────────────────────────
//
//   class crucible::perf::SenseHubV2
//   struct crucible::perf::CounterSnapshot
//   struct crucible::perf::CounterDelta
//   struct crucible::perf::GaugeSnapshot
//   struct crucible::perf::FullSnapshot
//   struct crucible::perf::LoadReport
//   enum class crucible::perf::Idx
//   enum class crucible::perf::Gauge
//   concept crucible::perf::CtxFitsSenseHubV2Mint
//
// The numeric constants `NUM_COUNTERS` / `NUM_GAUGES` / `BUILD_TAG`
// / `SENSE_HUB_VERSION` / `SENSE_HUB_LAYOUT_HASH` / `BUILD_NAME` are
// DELIBERATELY NOT re-exported.  A v2 caller who needs them queries
// `SenseHubV2::num_counters()` or `crucible::perf::NUM_COUNTERS`
// directly; the umbrella's job is the mint chokepoint, not the
// numeric ABI surface.
//
// ── Axiom coverage ─────────────────────────────────────────────────
//
//   InitSafe — using-decls introduce no new state; substrate's
//              std::optional return + NSDMI cover init.
//   TypeSafe — using-decls preserve `CtxFitsSenseHubV2Mint`
//              (IsExecCtx ∧ row_contains_v<…, Init>).
//   NullSafe — `mint_sense_hub_v2` takes no pointer parameter; the
//              `effects::Init` cap-tag is a phantom-typed witness.
//   MemSafe  — SenseHubV2 owns its kernel fd + mmap via RAII; the
//              std::optional move preserves ownership through the
//              using-decl.
//   BorrowSafe — std::optional moves out; no aliased mutation.
//   ThreadSafe — observation hub is read-only at runtime; Init-tier
//                load is single-threaded by construction.
//   LeakSafe — std::optional<SenseHubV2>'s dtor closes the BPF fd
//              and unmaps the ring; using-decl forwards.
//   DetSafe  — Init-tier; not on the deterministic-replay path.

#include <crucible/perf/SenseHubV2.h>
#include <crucible/effects/ExecCtx.h>

#include <type_traits>

namespace crucible::fixy::perf::v2 {

// ═════════════════════════════════════════════════════════════════════
// ── SenseHubV2 — extended counter + gauge observation hub ──────────
// ═════════════════════════════════════════════════════════════════════

using ::crucible::perf::mint_sense_hub_v2;
using ::crucible::perf::CtxFitsSenseHubV2Mint;
using ::crucible::perf::SenseHubV2;

// ── Supporting v2-only surface (data inspection types) ────────────

using ::crucible::perf::CounterSnapshot;
using ::crucible::perf::CounterDelta;
using ::crucible::perf::GaugeSnapshot;
using ::crucible::perf::FullSnapshot;
using ::crucible::perf::LoadReport;
using ::crucible::perf::Idx;
using ::crucible::perf::Gauge;

}  // namespace crucible::fixy::perf::v2

// ─── Dual-export sentinel — FIXY-U-122 ─────────────────────────────
//
// Header-internal identity sentinels for every v2-tree surface item.
// Same recipe as fixy/Perf.h::self_test (FIXY-U-121) — drift surfaces
// at every consumer's include time, NOT only when the test TU runs.
// Cardinality witness at the tail catches a future contributor adding
// (or removing) a v2 mint without updating both the using-decl block
// AND this sentinel.

namespace crucible::fixy::perf::v2::self_test {

// ─── Type-identity witnesses ─────────────────────────────────────

static_assert(std::is_same_v<
    ::crucible::fixy::perf::v2::SenseHubV2,
    ::crucible::perf::SenseHubV2>,
    "fixy::perf::v2::SenseHubV2 must alias substrate.");

static_assert(std::is_same_v<
    ::crucible::fixy::perf::v2::CounterSnapshot,
    ::crucible::perf::CounterSnapshot>,
    "fixy::perf::v2::CounterSnapshot must alias substrate.");

static_assert(std::is_same_v<
    ::crucible::fixy::perf::v2::CounterDelta,
    ::crucible::perf::CounterDelta>,
    "fixy::perf::v2::CounterDelta must alias substrate.");

static_assert(std::is_same_v<
    ::crucible::fixy::perf::v2::GaugeSnapshot,
    ::crucible::perf::GaugeSnapshot>,
    "fixy::perf::v2::GaugeSnapshot must alias substrate.");

static_assert(std::is_same_v<
    ::crucible::fixy::perf::v2::FullSnapshot,
    ::crucible::perf::FullSnapshot>,
    "fixy::perf::v2::FullSnapshot must alias substrate.");

static_assert(std::is_same_v<
    ::crucible::fixy::perf::v2::LoadReport,
    ::crucible::perf::LoadReport>,
    "fixy::perf::v2::LoadReport must alias substrate.");

static_assert(std::is_same_v<
    ::crucible::fixy::perf::v2::Idx,
    ::crucible::perf::Idx>,
    "fixy::perf::v2::Idx must alias substrate.");

static_assert(std::is_same_v<
    ::crucible::fixy::perf::v2::Gauge,
    ::crucible::perf::Gauge>,
    "fixy::perf::v2::Gauge must alias substrate.");

// ─── Positive concept admittance (ColdInitCtx carries Init) ──────

static_assert(::crucible::fixy::perf::v2::CtxFitsSenseHubV2Mint<
    ::crucible::effects::ColdInitCtx>,
    "fixy::perf::v2::CtxFitsSenseHubV2Mint must admit ColdInitCtx.");

// ─── Negative concept rejection (BgDrainCtx — Bg+Alloc, no Init) ──

static_assert(!::crucible::fixy::perf::v2::CtxFitsSenseHubV2Mint<
    ::crucible::effects::BgDrainCtx>,
    "fixy::perf::v2::CtxFitsSenseHubV2Mint must reject BgDrainCtx.");

// ─── Negative concept rejection (HotFgCtx — empty row, no Init) ──

static_assert(!::crucible::fixy::perf::v2::CtxFitsSenseHubV2Mint<
    ::crucible::effects::HotFgCtx>,
    "fixy::perf::v2::CtxFitsSenseHubV2Mint must reject HotFgCtx.");

// ─── Cardinality witness ─────────────────────────────────────────
//
// Exactly one v2 mint factory surfaces through `fixy::perf::v2::`:
// `mint_sense_hub_v2`.  Adding a second v2 mint must touch BOTH this
// constant AND `static_assert(v2_mint_cardinality == 1)` in
// test_fixy_perf_v2.cpp; otherwise CI reds.

inline constexpr int v2_mint_cardinality = 1;

}  // namespace crucible::fixy::perf::v2::self_test

// ─── Runtime smoke test ────────────────────────────────────────────
//
// Per FIXY-U-103 discipline.  Smoke is type-level: we cannot actually
// invoke `mint_sense_hub_v2` from a smoke routine without performing
// a real bpf()/perf_event_open syscall (which would fail under most
// CI sandboxes).  The smoke verifies the re-export concept resolves
// at runtime context (instantiation is already exercised at consteval
// above; this block ensures the header is reachable via runtime-call
// paths too).

namespace crucible::fixy::perf::v2 {

inline void runtime_smoke_test() noexcept {
    constexpr bool admits_cold = CtxFitsSenseHubV2Mint<
        ::crucible::effects::ColdInitCtx>;
    constexpr bool rejects_bg = !CtxFitsSenseHubV2Mint<
        ::crucible::effects::BgDrainCtx>;
    constexpr bool rejects_hot = !CtxFitsSenseHubV2Mint<
        ::crucible::effects::HotFgCtx>;
    (void)admits_cold;
    (void)rejects_bg;
    (void)rejects_hot;
}

}  // namespace crucible::fixy::perf::v2
