#pragma once

// ── crucible::cog::FitsCog — compile-time Row ≤ Cog capacity gate ───
//
// GAPS-191.  Per misc/03_05_2026_networking.md §4.4 + 25_04_2026.md §3.3.
// The LAST GATE before the Forge / partition-optimiser commits to a
// (Cog, kernel) binding.  Given a row-typed budget declaration
// (effects::ConcurrentRow<Tags...>) and a CogKind atom, FitsCog admits
// IFF every per-axis budget is ≤ the corresponding compile-time
// capacity ceiling for that CogKind's substrate.
//
// This eliminates a large family of oversubscription bugs at TYPE-CHECK
// time — long before they manifest as silent throughput loss in the
// scheduler:
//
//   1. Two compute kernels declaring SmBudget<x> + SmBudget<y> where
//      x + y > sm_count of any GPU we know — caught at template
//      substitution.  No GPU we ship to could possibly run this
//      schedule.
//
//   2. A NIC kernel declaring NicQp<m> on a GpuKind Cog — caught
//      because GPU's max-NicQp ceiling is 0 (NicQp is a network-
//      substrate axis) and any non-zero demand on a non-exposed axis
//      fails the comparison.
//
//   3. A network-coalescer demanding HbmBytes<huge> on a NicPort Cog
//      — caught for the same reason: NicPort's HbmBytes ceiling is 0.
//
//   4. A kernel demanding 999 SMs on Hopper — caught because the GPU
//      ceiling is the maximum across every shipped GPU we recognise
//      (Blackwell B200 = 256 SMs at the time of writing) and 999 > 256.
//
// ── What the gate does NOT do (deliberately) ────────────────────────
//
// FitsCog is the compile-time CEILING gate.  The compile-time ceilings
// are deliberately conservative — the LARGEST value any shipped Cog of
// that kind has.  This lets the gate pass a row that fits the largest
// hardware we recognise, and reject a row that fits NO hardware we
// recognise.  It does NOT replace the runtime caps check that confirms
// "this row fits THIS specific H100" — that's GAPS-196 cog/Calibrate.h's
// `caps_for_t<K>` runtime fields.  Two layers, complementary:
//
//   * FitsCog<Row, K>                    compile-time, "ANY Cog of kind K"
//   * fits_cog_caps_runtime(row, caps)   runtime, "THIS specific Cog"
//
// The runtime helper is provided here as a constexpr function the
// optimizer folds when caps are constexpr (vendor presets) and compiles
// to a simple uint64-comparison fold when caps are runtime-measured
// (calibration consumers).
//
// ── Soundness gates and how they fire (HS14 mandate) ────────────────
//
// FitsCog has TWO independently-witnessable mismatch classes — the
// HS14 distinct-mismatch-class invariant.  Each ships a negative-
// compile fixture demonstrating substitution failure:
//
//   Class A (CONCEPT gate, oversubscription):
//     A row demanding more than the Cog's per-axis ceiling fails at
//     template substitution.  Witness:
//       test/cog_neg/neg_fits_cog_oversubscribed.cpp
//     Mock factory `template <typename R> requires FitsCog<R, K> int f();`
//     called with a Row carrying SmBudget<999> against CogKind::Gpu.
//     The concept's Sm-axis comparison fails; the substitution fails;
//     the build fails.  Class: numeric exceedance on a SUBSTRATE-EXPOSED
//     axis.
//
//   Class B (CONCEPT gate, substrate validity):
//     A row tested against a non-substrate CogKind atom (PsuRail /
//     BmcSensor / OpticalTransceiver / aggregates) fails because
//     `cog_max_capacity<K>` has no specialisation for that K — the
//     `HasCogCapacity<K>` conjunct of FitsCog is false.  Witness:
//       test/cog_neg/neg_fits_cog_non_substrate.cpp
//     Mock factory called with PsuRail.  Distinct mismatch class:
//     STRUCTURAL substrate-binding failure (no caps to compare against).
//
// Per HS14, ≥2 negative-compile fixtures per new soundness gate, each
// demonstrating a distinct mismatch class.
//
// ── Append-only Universe extension (FOUND-I04) ──────────────────────
//
// Two append-only Universes feed into FitsCog:
//
//   * effects::ResourceKind catalog (23 atoms) — every axis a Row can
//     declare consumption on.  Renumbering an existing atom drifts
//     federation row_hash; new atoms append.  The 23-atom fold below
//     visits via reflection (`std::meta::enumerators_of(^^ResourceKind)`)
//     so a new atom auto-extends the comparison without manual edit
//     here — but the cog_max_capacity specialisations MUST add a case
//     for the new atom, otherwise the new atom gets the default-zero
//     ceiling and any non-zero demand on it fails on every Cog.
//     That's a CORRECT default — "we haven't enumerated this axis yet,
//     reject any demand for it" — but the ergonomic fix is to add the
//     case in every specialisation.
//
//   * cog::CogKind catalog (21 atoms) — every Cog level identity.
//     Adding a substrate atom (e.g., a future TpuPod with its own
//     TargetCaps schema) requires:
//       1. Append CogKind enumerator at next free underlying value.
//       2. Add the corresponding TargetCaps schema.
//       3. Add caps_for<K> specialisation.
//       4. Add cog_max_capacity<K> specialisation HERE so FitsCog
//          recognises it as a substrate.
//     Step 4 is the load-bearing one — without it, FitsCog rejects
//     the new atom as non-substrate (Class B above).
//
//   Axiom coverage: TypeSafe — Row demands and Cog ceilings are
//                   strong-typed (uint64_t over a typed enum); no
//                   silent narrowing, no implicit conversion at the
//                   comparison point.
//                   DetSafe — every comparison is consteval; result is
//                   stable across compilers / TUs / fleets.
//                   InitSafe — primary `cog_max_capacity<K>` is
//                   undefined; querying an unspecialised K fails at
//                   substitution rather than reading default-zero.
//
//   Runtime cost:   zero — the gate lives entirely in template
//                   substitution and consteval evaluation.  At runtime
//                   the only effect is that an oversubscribed schedule
//                   simply cannot be expressed at all.
//
// ── Gates ───────────────────────────────────────────────────────────
//
//   Consumed by:
//     GAPS-188 mimic/CogMimic.h         — per-Cog factory checks Row
//                                          fits before binding.
//     GAPS-167 forge/Ir001/Comm.h       — kernel-author ergonomics.
//     GAPS-810 partition optimiser      — pre-filters Cog-binding
//                                          candidates by static
//                                          ceiling check before the
//                                          calibrated cost surface.
//     GAPS-165 AdaptiveOptimizer         — concurrent-schedule check
//                                          combines GAPS-190 row-sum
//                                          with FitsCog ceiling read.
//
//   Depends on:
//     cog/CogIdentity.h                 — CogKind atom catalog.
//     cog/TargetCaps.h                  — caps_for<K> schemas (for the
//                                         runtime overload).
//     effects/Resources.h               — ResourceKind atom catalog.
//     effects/Concurrent.h              — ConcurrentRow + per-kind
//                                         value lookup.
//
// References:
//   misc/03_05_2026_networking.md §4.4  (compile-time budget verification)
//   25_04_2026.md §3.3                  (Met(X) row machinery)
//   Tang-Lindley POPL 2026 (arXiv:2507.10301)
//   CRUCIBLE.md L2                      (Forge / Mimic substrate schemas)

#include <crucible/cog/CogIdentity.h>
#include <crucible/cog/TargetCaps.h>
#include <crucible/effects/Concurrent.h>
#include <crucible/effects/Resources.h>

#include <concepts>
#include <cstdint>
#include <meta>
#include <type_traits>

namespace crucible::cog {

// ────────────────────────────────────────────────────────────────────
// cog_max_capacity<K> — per-CogKind compile-time per-axis ceiling
// ────────────────────────────────────────────────────────────────────
//
// Each substrate CogKind specialises this to publish, for every
// effects::ResourceKind axis, the LARGEST capacity any shipped Cog of
// that kind has — the conservative ceiling.  Axes the Cog kind does
// not expose return 0; any non-zero demand on a 0-ceiling axis fails
// the comparison, which is the desired "this Cog has no capacity for
// this axis" semantics.
//
// The primary template is INTENTIONALLY undefined.  Querying
// `cog_max_capacity<K>::for_kind(...)` for an unspecialised K fails
// at name lookup, which the `HasCogCapacity<K>` concept gate catches
// as substrate-binding failure (HS14 fixture #2 witnesses this).
//
// Underlying ceiling values are derived from publicly-shipped silicon
// at the time of writing (2026-05).  Values are CEILINGS, not nominal
// — bumping them when newer hardware ships is a non-breaking event
// (raises admission, never lowers); reducing them is a wire-format
// break for any Row that previously fit but now doesn't.

template <CogKind K>
struct cog_max_capacity;  // primary INTENTIONALLY undefined

// ── GpuTargetCaps — covers every shipped GPU substrate ─────────────
//
// Ceilings span Hopper (H100/H200, 132/144 SMs, 80/141 GB HBM, 3.35
// TB/s HBM bw), Blackwell (B100/B200, 192/256 SMs, 192 GB HBM, 8 TB/s
// HBM bw), AMD CDNA3+ (MI300X 304 CUs / 192 GB HBM3, MI325X 304 CUs /
// 288 GB HBM3E / 6 TB/s), and headroom for next-gen (B300, MI400X).
// Ceiling = MAX across the family + ~30% headroom.  Per the
// append-only-ceiling discipline at the top of this header, raising
// these ceilings is non-breaking (raises admission); LOWERING any
// ceiling is a wire-format break for any Row that previously fit.
template <>
struct cog_max_capacity<CogKind::Gpu> {
    // constexpr (NOT consteval) so the runtime smoke-test discipline
    // can drive every ResourceKind atom through this accessor with
    // non-constant volatile arguments.  The compile-time gate still
    // works (static_assert / concept substitution forces constant
    // evaluation); only runtime callers see this as a regular
    // constexpr function.  Mirrors resource_kind_name() in
    // effects/Resources.h.
    static constexpr std::uint64_t for_kind(effects::ResourceKind k) noexcept {
        using effects::ResourceKind;
        switch (k) {
            // GPU compute substrate
            case ResourceKind::Sm:                return 320ULL;                       // Blackwell B200=256, headroom
            case ResourceKind::WarpScheduler:     return 320ULL * 4;                   // 4 schedulers/SM
            case ResourceKind::RegistersPerWarp:  return 65'536ULL;                    // 256 KB regs / 4 B
            case ResourceKind::Smem:              return 320ULL * 256ULL * 1024;       // sm * 256 KB
            case ResourceKind::L2:                return 100ULL * 1024 * 1024;         // 100 MB (B200)
            // GPU memory substrate
            case ResourceKind::HbmBytes:          return 384ULL * 1024 * 1024 * 1024;  // 384 GB (MI325X=288GB+headroom)
            case ResourceKind::HbmBw:             return 9ULL * 1024 * 1024 * 1024 * 1024;  // 9 TB/s
            // Inter-device
            case ResourceKind::NvlinkBw:          return 1'800ULL * 1024 * 1024 * 1024;  // 1.8 TB/s (NVL5)
            // PcieBw deliberately = 0 here.  PCIe bandwidth is computed
            // from caps.pcie_gen + caps.pcie_lanes; no derived field
            // ships in TargetCaps yet.  Setting the compile-time
            // ceiling = 0 keeps the runtime overload (which also
            // returns 0 from caps_runtime_capacity::for_kind for
            // PcieBw) consistent with the compile-time gate — both
            // reject any PcieBw demand.  When cog/Calibrate.h ships
            // the derived `pcie_bw_bytes_per_sec` field, bump this
            // ceiling to PCIe6 x16 raw rate and update the runtime
            // mapping to read the derived field.  Until then,
            // refusing PcieBw demand is conservatively-correct.
            case ResourceKind::PcieBw:            return 0ULL;
            // Power / thermal
            case ResourceKind::PowerWatts:        return 1'500ULL;                     // B200 = 1000W, headroom
            case ResourceKind::ThermalCelsius:    return 95ULL;                        // common throttle
            // GPU does not expose: NIC*, Switch*, Tcam, CpuCore, Llc,
            // RackPowerKw, CarbonGramsPerKwh — all default 0.
            default: return 0ULL;
        }
    }
};

// ── NicPortTargetCaps — covers every shipped NIC port ──────────────
//
// Ceilings span ConnectX-7 (400 GbE, 16M QPs, 64K MR), ConnectX-8 (800
// GbE planned), Broadcom Thor2 (400 GbE), Intel E810 (200 GbE).
// Ceiling = MAX with 2x headroom for next-gen 800/1.6T NICs.
template <>
struct cog_max_capacity<CogKind::NicPort> {
    static constexpr std::uint64_t for_kind(effects::ResourceKind k) noexcept {
        using effects::ResourceKind;
        switch (k) {
            // PcieBw deliberately = 0 — see GpuTargetCaps comment for
            // rationale.  Both compile-time ceiling and runtime helper
            // refuse PcieBw demand consistently until Calibrate.h
            // ships the derived field.
            case ResourceKind::PcieBw:            return 0ULL;
            // NIC substrate
            case ResourceKind::NicQ:              return 256ULL;                        // tx+rx queues
            case ResourceKind::NicRing:           return 64ULL * 1024;                  // ring slots
            case ResourceKind::NicQp:             return 16ULL * 1024 * 1024;           // ConnectX-7 max
            case ResourceKind::NicCq:             return 16ULL * 1024 * 1024;           // matches QP
            case ResourceKind::NicMr:             return 64ULL * 1024;                  // memory regions
            // Power / thermal
            case ResourceKind::PowerWatts:        return 50ULL;                         // NIC TDP envelope
            case ResourceKind::ThermalCelsius:    return 85ULL;
            // NIC does not expose: GPU compute, GPU memory, NvlinkBw,
            // Switch*, Tcam, CpuCore, Llc, RackPowerKw — default 0.
            default: return 0ULL;
        }
    }
};

// ── NvSwitchTargetCaps — fabric atom ───────────────────────────────
//
// Ceilings cover NVSwitch4 (NVL72: 64 ports × 200 GB/s = 12.8 TB/s
// aggregate, ~32 KB buffer cells, ~16K TCAM entries) plus 2x headroom.
template <>
struct cog_max_capacity<CogKind::NvSwitch> {
    static constexpr std::uint64_t for_kind(effects::ResourceKind k) noexcept {
        using effects::ResourceKind;
        switch (k) {
            // Switch / fabric
            case ResourceKind::SwitchEgressBw:    return 32ULL * 1024 * 1024 * 1024 * 1024;  // 32 TB/s headroom
            case ResourceKind::SwitchBuffer:      return 128ULL * 1024;                       // 128K cells
            case ResourceKind::Tcam:              return 64ULL * 1024;                        // 64K entries
            // Power / thermal
            case ResourceKind::PowerWatts:        return 1'500ULL;
            case ResourceKind::ThermalCelsius:    return 90ULL;
            // Switch does not expose compute, memory, NIC*, host axes.
            default: return 0ULL;
        }
    }
};

// ── CpuCoreTargetCaps — atomic CPU core ────────────────────────────
//
// Single core: 1 CpuCore, ~2 MB private L2 (Granite Rapids), no LLC
// (lives at socket).  TDP per core ~25 W.
template <>
struct cog_max_capacity<CogKind::CpuCore> {
    static constexpr std::uint64_t for_kind(effects::ResourceKind k) noexcept {
        using effects::ResourceKind;
        switch (k) {
            case ResourceKind::CpuCore:           return 1ULL;                          // atomic = 1
            case ResourceKind::L2:                return 4ULL * 1024 * 1024;            // 4 MB private L2
            case ResourceKind::PowerWatts:        return 50ULL;                          // per-core envelope
            case ResourceKind::ThermalCelsius:    return 100ULL;                         // CPU Tjmax
            default: return 0ULL;
        }
    }
};

// ── CpuSocketTargetCaps — L1 aggregate of CPU cores ────────────────
//
// AMD EPYC 9005 (128 cores, 256 threads, 384 MB V-Cache LLC, 600 W TDP),
// Intel Granite Rapids (96 cores, 480 MB LLC), with 2x headroom.
template <>
struct cog_max_capacity<CogKind::CpuSocket> {
    static constexpr std::uint64_t for_kind(effects::ResourceKind k) noexcept {
        using effects::ResourceKind;
        switch (k) {
            case ResourceKind::CpuCore:           return 256ULL;                        // current max + headroom
            case ResourceKind::Llc:               return 1024ULL * 1024 * 1024;         // 1 GB headroom
            case ResourceKind::L2:                return 256ULL * 4ULL * 1024 * 1024;   // socket * 4MB
            case ResourceKind::PowerWatts:        return 800ULL;                        // EPYC 600W + headroom
            case ResourceKind::ThermalCelsius:    return 100ULL;
            default: return 0ULL;
        }
    }
};

// ── DramChannelTargetCaps — atomic DDR/HBM/LPDDR channel ───────────
//
// Per-channel: ~50 GB/s (DDR5-6400 dual rank), 256 GB capacity (DIMM
// stack).  Bandwidth = HbmBw axis (treated as "off-chip memory bw").
// DRAM is not currently a budget-row consumer for compute kernels —
// row-level memory is accounted at the GPU's HbmBytes/HbmBw axes.  A
// future "HostDramBytes" axis would land here.
template <>
struct cog_max_capacity<CogKind::DramChannel> {
    static constexpr std::uint64_t for_kind(effects::ResourceKind k) noexcept {
        using effects::ResourceKind;
        switch (k) {
            // DRAM channel exposes capacity + bandwidth only.  No first-
            // class budget axis exists yet for "host DRAM bytes" — when
            // that axis lands (effects::Resources extension), update
            // here.  Until then, DRAM has no exposed budget axis.
            default: return 0ULL;
        }
    }
};

// ── HasCogCapacity<K> concept ──────────────────────────────────────
//
// True iff `cog_max_capacity<K>` is specialised — i.e., K is a
// substrate CogKind for which we publish a per-axis ceiling.
// The substitution `cog_max_capacity<K>::for_kind(...)` for an
// unspecialised K fails because the primary template is incomplete;
// the `requires` clause catches that as substitution failure.
//
// Composes with `HasCaps<K>` (TargetCaps.h): every substrate that
// publishes caps_for ALSO publishes cog_max_capacity (we ship both
// in lockstep).  HS14 fixture #2 (neg_fits_cog_non_substrate.cpp)
// witnesses the rejection on PsuRail.
template <CogKind K>
concept HasCogCapacity = requires(effects::ResourceKind axis) {
    { cog_max_capacity<K>::for_kind(axis) } -> std::same_as<std::uint64_t>;
};

// ────────────────────────────────────────────────────────────────────
// FitsCog<Row, K> — the load-bearing soundness gate
// ────────────────────────────────────────────────────────────────────
//
// True iff Row is a well-formed ConcurrentRow AND K is a substrate
// AND every per-axis demand fits within K's compile-time ceiling.

namespace detail {

// 23-fold over ResourceKind atoms, comparing per-axis demand vs
// ceiling.  Reflection-driven: a future ResourceKind atom auto-
// participates in the fold without manual edit here (but the
// cog_max_capacity specialisations need the new case OR the new atom
// gets ceiling=0 and any non-zero demand fails on every Cog —
// conservatively-correct default).
//
// Naming: "evaluate" is the verb (the action this consteval performs);
// the previous name `row_fits_cog_compute` conflated the verb with
// CogFamily::Compute — the function actually evaluates row-vs-Cog fit
// for any substrate K (Compute / Network / Memory / Bus families) per
// the broader IsMimicSubstrate gate.
template <typename Row, CogKind K>
[[nodiscard]] consteval bool evaluate_row_fits_cog() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^effects::ResourceKind));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        constexpr effects::ResourceKind axis = [:en:];
        constexpr std::uint64_t demand =
            effects::concurrent_row_value_v<axis, Row>;
        constexpr std::uint64_t ceiling =
            cog_max_capacity<K>::for_kind(axis);
        if (demand > ceiling) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}

template <typename Row, CogKind K>
inline constexpr bool row_fits_cog_v = evaluate_row_fits_cog<Row, K>();

}  // namespace detail

// FitsCog<Row, K>:
//   1. Row IsConcurrentRow (well-formed budget carrier);
//   2. K HasCogCapacity (substrate CogKind we recognise);
//   3. every axis demand ≤ ceiling.
template <typename Row, CogKind K>
concept FitsCog =
    effects::IsConcurrentRow<Row> &&
    HasCogCapacity<K> &&
    detail::row_fits_cog_v<Row, K>;

// ────────────────────────────────────────────────────────────────────
// fits_cog_caps_runtime — runtime overload for measured caps
// ────────────────────────────────────────────────────────────────────
//
// Optional companion to the compile-time FitsCog gate: given a Row
// (compile-time-typed) and a runtime caps_for_t<K> instance (with
// vendor / calibrated values), check if the row fits the SPECIFIC
// Cog instance (not just the family).  Used by per-Cog binders
// (GAPS-188 mimic/CogMimic.h, GAPS-810 partition optimiser).
//
// The implementation reads each axis's demand at compile time and
// each axis's capacity from the caps struct at runtime, comparing.
// For axes not exposed by the caps schema, the function returns the
// per-kind compile-time ceiling treatment (capacity = 0 → demand 0
// passes, demand > 0 fails).
//
// Per-CogKind specialisations live below to map ResourceKind axes
// to the corresponding caps struct fields (e.g., GPU's HbmBytes axis
// ↔ caps.hbm_bytes.value()).  The mapping is a small, mechanical
// switch that mirrors cog_max_capacity's per-axis cases.

namespace detail {

// Read caps's per-axis capacity at runtime.  Specialised per CogKind
// so the field-level mapping is co-located with the caps schema.
template <CogKind K>
struct caps_runtime_capacity;  // primary undefined

template <>
struct caps_runtime_capacity<CogKind::Gpu> {
    [[nodiscard]] static constexpr std::uint64_t
    for_kind(GpuTargetCaps const& caps,
             effects::ResourceKind axis) noexcept {
        using effects::ResourceKind;
        switch (axis) {
            case ResourceKind::Sm:
                return std::uint64_t{caps.sm_count.value()};
            case ResourceKind::WarpScheduler:
                return std::uint64_t{caps.sm_count.value()} *
                       std::uint64_t{caps.warp_schedulers_per_sm.value()};
            case ResourceKind::RegistersPerWarp:
                return std::uint64_t{caps.max_regs_per_thread.value()} *
                       std::uint64_t{caps.warp_size.value()};
            case ResourceKind::Smem:
                return std::uint64_t{caps.sm_count.value()} *
                       std::uint64_t{caps.smem_per_sm_bytes.value()};
            case ResourceKind::L2:
                return caps.l2_bytes.value();
            case ResourceKind::HbmBytes:
                return caps.hbm_bytes.value();
            case ResourceKind::HbmBw:
                return caps.hbm_bandwidth_bytes_per_sec.value();
            case ResourceKind::NvlinkBw:
                return caps.nvlink_bandwidth_bytes_per_sec.value();
            // PcieBw is computed from pcie_gen + pcie_lanes — left to
            // the calibrator to populate explicitly via a derived field
            // when the calibrator fills runtime caps.  For now reading
            // 0 here is conservatively rejecting any PcieBw demand — a
            // future cog/Calibrate.h commit lifts this once the derived
            // pcie_bw_bytes_per_sec field lands.
            case ResourceKind::PcieBw:
                return 0ULL;
            case ResourceKind::PowerWatts:
                return std::uint64_t{caps.tdp_watts.value()};
            case ResourceKind::ThermalCelsius:
                return std::uint64_t{caps.thermal_throttle_celsius.value()};
            default: return 0ULL;
        }
    }
};

template <>
struct caps_runtime_capacity<CogKind::NicPort> {
    [[nodiscard]] static constexpr std::uint64_t
    for_kind(NicPortTargetCaps const& caps,
             effects::ResourceKind axis) noexcept {
        using effects::ResourceKind;
        switch (axis) {
            case ResourceKind::NicQ:
                return std::uint64_t{caps.max_tx_queues.value()} +
                       std::uint64_t{caps.max_rx_queues.value()};
            case ResourceKind::NicQp:
                return std::uint64_t{caps.max_qp_count.value()};
            case ResourceKind::NicCq:
                return std::uint64_t{caps.max_cq_count.value()};
            case ResourceKind::NicMr:
                return std::uint64_t{caps.max_mr_count.value()};
            case ResourceKind::Tcam:
                return std::uint64_t{caps.tcam_entries.value()};
            case ResourceKind::PcieBw:
                return 0ULL;  // see GpuTargetCaps comment above
            default: return 0ULL;
        }
    }
};

template <>
struct caps_runtime_capacity<CogKind::NvSwitch> {
    [[nodiscard]] static constexpr std::uint64_t
    for_kind(NvSwitchTargetCaps const& caps,
             effects::ResourceKind axis) noexcept {
        using effects::ResourceKind;
        switch (axis) {
            case ResourceKind::SwitchEgressBw:
                return caps.aggregate_bandwidth_bytes_per_sec.value();
            case ResourceKind::SwitchBuffer:
                return caps.buffer_bytes.value();
            case ResourceKind::Tcam:
                return std::uint64_t{caps.tcam_entries.value()};
            default: return 0ULL;
        }
    }
};

template <>
struct caps_runtime_capacity<CogKind::CpuCore> {
    [[nodiscard]] static constexpr std::uint64_t
    for_kind(CpuCoreTargetCaps const& caps,
             effects::ResourceKind axis) noexcept {
        using effects::ResourceKind;
        switch (axis) {
            case ResourceKind::CpuCore:
                return 1ULL;
            case ResourceKind::L2:
                return std::uint64_t{caps.l2_bytes.value()};
            default: return 0ULL;
        }
    }
};

template <>
struct caps_runtime_capacity<CogKind::CpuSocket> {
    [[nodiscard]] static constexpr std::uint64_t
    for_kind(CpuSocketTargetCaps const& caps,
             effects::ResourceKind axis) noexcept {
        using effects::ResourceKind;
        switch (axis) {
            case ResourceKind::CpuCore:
                return std::uint64_t{caps.core_count.value()};
            case ResourceKind::Llc:
                return caps.l3_bytes.value();
            case ResourceKind::PowerWatts:
                return std::uint64_t{caps.tdp_watts.value()};
            case ResourceKind::ThermalCelsius:
                return std::uint64_t{caps.thermal_throttle_celsius.value()};
            default: return 0ULL;
        }
    }
};

template <>
struct caps_runtime_capacity<CogKind::DramChannel> {
    [[nodiscard]] static constexpr std::uint64_t
    for_kind(DramChannelTargetCaps const& /*caps*/,
             effects::ResourceKind /*axis*/) noexcept {
        // No first-class budget axis maps to DramChannel today.  Future
        // "HostDramBytes" axis would land here.
        return 0ULL;
    }
};

}  // namespace detail

// Public helper — given Row + CogKind + caps_for_t<K> instance, check
// if the row fits.  When caps fields are constexpr (vendor presets),
// the optimizer folds this to a constant.  When caps fields are
// runtime (calibrator output), this compiles to a per-axis
// uint64-comparison fold.
//
// The function is reflection-driven over ResourceKind atoms — a new
// atom auto-extends the comparison.  The corresponding
// caps_runtime_capacity specialisation must add a case for the new
// atom OR the new atom defaults to capacity=0 (any non-zero demand
// fails — conservatively-correct).
template <typename Row, CogKind K>
    requires effects::IsConcurrentRow<Row> && HasCogCapacity<K>
[[nodiscard]] constexpr bool
fits_cog_caps_runtime(caps_for_t<K> const& caps) noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^effects::ResourceKind));
    bool fits = true;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        constexpr effects::ResourceKind axis = [:en:];
        constexpr std::uint64_t demand =
            effects::concurrent_row_value_v<axis, Row>;
        if (demand > 0) {
            const std::uint64_t capacity =
                detail::caps_runtime_capacity<K>::for_kind(caps, axis);
            if (demand > capacity) {
                fits = false;
            }
        }
    }
#pragma GCC diagnostic pop
    return fits;
}

// ────────────────────────────────────────────────────────────────────
// Self-test block
// ────────────────────────────────────────────────────────────────────
namespace detail::fits_cog_self_test {

// ── HasCogCapacity coverage ────────────────────────────────────────
//
// Every substrate CogKind we ship a TargetCaps for ALSO ships a
// cog_max_capacity specialisation.  This keeps the two binding
// tables in lockstep — a future substrate atom that updates one
// without the other fails this assertion.
static_assert(HasCogCapacity<CogKind::Gpu>);
static_assert(HasCogCapacity<CogKind::CpuCore>);
static_assert(HasCogCapacity<CogKind::CpuSocket>);
static_assert(HasCogCapacity<CogKind::NicPort>);
static_assert(HasCogCapacity<CogKind::NvSwitch>);
static_assert(HasCogCapacity<CogKind::DramChannel>);

// Non-substrate CogKinds are rejected by HasCogCapacity (HS14 fixture
// #2's STRUCTURAL gate).  PsuRail / BmcSensor / OpticalTransceiver /
// NvmeNamespace / PcieLaneGroup all have no cog_max_capacity
// specialisation — substitution fails.
static_assert(!HasCogCapacity<CogKind::PsuRail>);
static_assert(!HasCogCapacity<CogKind::BmcSensor>);
static_assert(!HasCogCapacity<CogKind::OpticalTransceiver>);
static_assert(!HasCogCapacity<CogKind::NvmeNamespace>);
static_assert(!HasCogCapacity<CogKind::PcieLaneGroup>);
// L1..L7 aggregate kinds also have no caps schema.
static_assert(!HasCogCapacity<CogKind::Datacenter>);
static_assert(!HasCogCapacity<CogKind::Rack>);
static_assert(!HasCogCapacity<CogKind::Server>);
static_assert(!HasCogCapacity<CogKind::GpuPackage>);
static_assert(!HasCogCapacity<CogKind::NicCard>);

// ── HasCogCapacity ↔ HasCaps consistency ───────────────────────────
//
// Both binding tables (cog_max_capacity, caps_for) must be in
// lockstep.  A substrate that publishes one but not the other is a
// soundness break — the runtime overload fits_cog_caps_runtime
// references caps_for_t<K> which only resolves when both are
// specialised.
static_assert(HasCaps<CogKind::Gpu> == HasCogCapacity<CogKind::Gpu>);
static_assert(HasCaps<CogKind::CpuCore> == HasCogCapacity<CogKind::CpuCore>);
static_assert(HasCaps<CogKind::CpuSocket> == HasCogCapacity<CogKind::CpuSocket>);
static_assert(HasCaps<CogKind::NicPort> == HasCogCapacity<CogKind::NicPort>);
static_assert(HasCaps<CogKind::NvSwitch> == HasCogCapacity<CogKind::NvSwitch>);
static_assert(HasCaps<CogKind::DramChannel> == HasCogCapacity<CogKind::DramChannel>);
static_assert(HasCaps<CogKind::PsuRail> == HasCogCapacity<CogKind::PsuRail>);

// ── caps_runtime_capacity lockstep with cog_max_capacity ───────────
//
// The runtime overload `fits_cog_caps_runtime<Row, K>(caps)` calls
// `caps_runtime_capacity<K>::for_kind(...)`.  If a future
// contributor adds a `cog_max_capacity<K>` specialisation but
// forgets the matching `caps_runtime_capacity<K>` specialisation,
// the substitution failure surfaces only when someone first calls
// `fits_cog_caps_runtime<..., K>` — far from the omission site.
// These static_asserts catch the omission HERE, in the same TU as
// the binding tables, so the diagnostic points at the missing
// specialisation rather than a remote consumer's call site.
//
// Witness "specialised" by proving for_kind is callable on
// caps_for_t<K>.  A new substrate that ships cog_max_capacity but
// forgets caps_runtime_capacity trips the static_asserts below
// (false witness on a substrate we expect to ship).  A non-
// substrate K fails because caps_for_t<K> doesn't resolve.
template <CogKind K>
inline constexpr bool has_caps_runtime_capacity_v = requires(caps_for_t<K> const& caps,
                                                              effects::ResourceKind axis) {
    { detail::caps_runtime_capacity<K>::for_kind(caps, axis) }
        -> std::same_as<std::uint64_t>;
};
static_assert(has_caps_runtime_capacity_v<CogKind::Gpu>);
static_assert(has_caps_runtime_capacity_v<CogKind::NicPort>);
static_assert(has_caps_runtime_capacity_v<CogKind::NvSwitch>);
static_assert(has_caps_runtime_capacity_v<CogKind::CpuCore>);
static_assert(has_caps_runtime_capacity_v<CogKind::CpuSocket>);
static_assert(has_caps_runtime_capacity_v<CogKind::DramChannel>);
// Non-substrate Cogs reject (no caps_for_t<K> specialization).
static_assert(!has_caps_runtime_capacity_v<CogKind::PsuRail>);
static_assert(!has_caps_runtime_capacity_v<CogKind::BmcSensor>);
static_assert(!has_caps_runtime_capacity_v<CogKind::Datacenter>);

// ── Per-substrate ceiling sanity — spot-check non-zero on at least
//    ONE axis the substrate exposes ───────────────────────────────
//
// Every substrate exposes at least one budget axis with non-zero
// ceiling.  A specialisation that returns 0 for every axis is a
// permanent reject-everything sentinel — almost always wrong.
//
// Note DramChannel is the deliberate exception: no axis exposed yet
// (future "HostDramBytes" axis lands when the resource catalog grows).
static_assert(cog_max_capacity<CogKind::Gpu>::for_kind(effects::ResourceKind::Sm) > 0);
static_assert(cog_max_capacity<CogKind::Gpu>::for_kind(effects::ResourceKind::HbmBytes) > 0);
static_assert(cog_max_capacity<CogKind::CpuCore>::for_kind(effects::ResourceKind::CpuCore) > 0);
static_assert(cog_max_capacity<CogKind::CpuSocket>::for_kind(effects::ResourceKind::CpuCore) > 0);
static_assert(cog_max_capacity<CogKind::NicPort>::for_kind(effects::ResourceKind::NicQp) > 0);
static_assert(cog_max_capacity<CogKind::NvSwitch>::for_kind(effects::ResourceKind::SwitchEgressBw) > 0);

// ── Cross-substrate axis isolation ─────────────────────────────────
//
// A NIC's GPU axes are 0; a GPU's NIC axes are 0; a switch's compute
// axes are 0.  This is the load-bearing axis-mismatch witness — the
// "Class B except at the within-substrate-kind level": e.g., a Row
// demanding NicQp on a GPU CogKind fails because GPU's NicQp ceiling
// is 0.
static_assert(cog_max_capacity<CogKind::Gpu>::for_kind(effects::ResourceKind::NicQp) == 0);
static_assert(cog_max_capacity<CogKind::Gpu>::for_kind(effects::ResourceKind::SwitchEgressBw) == 0);
static_assert(cog_max_capacity<CogKind::Gpu>::for_kind(effects::ResourceKind::CpuCore) == 0);
static_assert(cog_max_capacity<CogKind::NicPort>::for_kind(effects::ResourceKind::Sm) == 0);
static_assert(cog_max_capacity<CogKind::NicPort>::for_kind(effects::ResourceKind::HbmBytes) == 0);
static_assert(cog_max_capacity<CogKind::NvSwitch>::for_kind(effects::ResourceKind::Sm) == 0);
static_assert(cog_max_capacity<CogKind::NvSwitch>::for_kind(effects::ResourceKind::NicQp) == 0);
static_assert(cog_max_capacity<CogKind::CpuCore>::for_kind(effects::ResourceKind::Sm) == 0);
static_assert(cog_max_capacity<CogKind::CpuCore>::for_kind(effects::ResourceKind::NicQp) == 0);

// ── FitsCog: positive admission ────────────────────────────────────
//
// Realistic Hopper-sized row admits.  H100 = 132 SMs, 80 GB HBM, 3.35
// TB/s HBM bw; well under the GPU ceiling.
using H100ComputeRow = effects::ConcurrentRow<
    effects::SmBudget<132>,
    effects::HbmBytes<80'000'000'000ULL>>;
static_assert(FitsCog<H100ComputeRow, CogKind::Gpu>);

// Realistic 400G NIC row admits.
using NicAllReduceRow = effects::ConcurrentRow<
    effects::NicQp<4>,
    effects::NicCq<4>,
    effects::NicMr<8>>;
static_assert(FitsCog<NicAllReduceRow, CogKind::NicPort>);

// NVSwitch fabric row admits.
using SwitchRow = effects::ConcurrentRow<
    effects::SwitchEgressBw<400'000'000'000ULL>,  // 400 GB/s
    effects::SwitchBufferCells<32 * 1024>>;
static_assert(FitsCog<SwitchRow, CogKind::NvSwitch>);

// CPU socket row.
using SocketRow = effects::ConcurrentRow<
    effects::CpuCoreBudget<64>,
    effects::LlcBytes<128 * 1024 * 1024>>;
static_assert(FitsCog<SocketRow, CogKind::CpuSocket>);

// Empty row admits anywhere (vacuously satisfies every axis).
static_assert(FitsCog<effects::ConcurrentRow<>, CogKind::Gpu>);
static_assert(FitsCog<effects::ConcurrentRow<>, CogKind::NicPort>);
static_assert(FitsCog<effects::ConcurrentRow<>, CogKind::NvSwitch>);

// ── FitsCog: oversubscription rejection (HS14 Class A in-header) ──
//
// Row demanding more SMs than ANY shipped GPU has — rejected.
using OversubscribedSmRow = effects::ConcurrentRow<effects::SmBudget<999>>;
static_assert(!FitsCog<OversubscribedSmRow, CogKind::Gpu>);

// HBM > ceiling.
using OversubscribedHbmRow = effects::ConcurrentRow<
    effects::HbmBytes<512ULL * 1024 * 1024 * 1024>>;  // 512 GB > 384 GB ceiling
static_assert(!FitsCog<OversubscribedHbmRow, CogKind::Gpu>);

// Two summed budgets that together exceed ceiling: SmBudget<200> +
// SmBudget<200> in concurrent-schedule sum = 400 > 320 ceiling.  Note
// FitsCog itself takes a Row (already-summed); the GAPS-190 layer is
// what computes the sum.  We exercise post-sum here.
using ConcurrentOverSubRow = effects::concurrent_row_sum_t<
    effects::ConcurrentRow<effects::SmBudget<200>>,
    effects::ConcurrentRow<effects::SmBudget<200>>>;
static_assert(!FitsCog<ConcurrentOverSubRow, CogKind::Gpu>);
static_assert(effects::concurrent_row_value_v<effects::ResourceKind::Sm,
              ConcurrentOverSubRow> == 400);

// ── FitsCog: cross-substrate axis-mismatch rejection (HS14 Class B
//            in-header) ───────────────────────────────────────────
//
// Row demanding NIC QPs on a GPU CogKind — GPU's NicQp ceiling is 0.
// Any non-zero demand on a 0-ceiling axis fails.
using NicDemandOnGpu = effects::ConcurrentRow<effects::NicQp<4>>;
static_assert(!FitsCog<NicDemandOnGpu, CogKind::Gpu>);

// Row demanding SMs on a NicPort CogKind — NicPort's Sm ceiling is 0.
using GpuDemandOnNic = effects::ConcurrentRow<effects::SmBudget<4>>;
static_assert(!FitsCog<GpuDemandOnNic, CogKind::NicPort>);

// Row demanding switch egress bw on a CPU socket — CpuSocket's
// SwitchEgressBw ceiling is 0.
using SwitchDemandOnCpu = effects::ConcurrentRow<
    effects::SwitchEgressBw<100'000'000'000ULL>>;
static_assert(!FitsCog<SwitchDemandOnCpu, CogKind::CpuSocket>);

// ── FitsCog: substrate-validity rejection (HS14 Class B underlying) ─
//
// Row tested against a non-substrate CogKind.  HasCogCapacity<K>
// fails; the FitsCog conjunct fails.  The HS14 fixture
// neg_fits_cog_non_substrate.cpp witnesses this in a constrained-
// template-substitution context.
static_assert(!FitsCog<H100ComputeRow, CogKind::PsuRail>);
static_assert(!FitsCog<H100ComputeRow, CogKind::BmcSensor>);
static_assert(!FitsCog<H100ComputeRow, CogKind::OpticalTransceiver>);
static_assert(!FitsCog<effects::ConcurrentRow<>, CogKind::PsuRail>);

// ── FitsCog: shape rejection (well-formedness) ─────────────────────
//
// FitsCog requires Row to be a ConcurrentRow.  Plain types, scalar
// Tag values, and effects::Row<Es...> (a different row family) do
// NOT satisfy the IsConcurrentRow gate.
static_assert(!FitsCog<int, CogKind::Gpu>);
static_assert(!FitsCog<effects::resource::SmBudget<32>, CogKind::Gpu>);

// ── Boundary: demand exactly == ceiling admits (edge of pass) ─────
//
// The comparison is `demand > ceiling`, NOT `demand >= ceiling`.  A
// row that exactly saturates the ceiling still admits — there's a Cog
// whose capacity equals the demand.
using SaturateGpuSm = effects::ConcurrentRow<effects::SmBudget<320>>;
static_assert(FitsCog<SaturateGpuSm, CogKind::Gpu>);

using SaturateGpuHbm = effects::ConcurrentRow<
    effects::HbmBytes<384ULL * 1024 * 1024 * 1024>>;
static_assert(FitsCog<SaturateGpuHbm, CogKind::Gpu>);

// ── Boundary: demand = ceiling + 1 fails ───────────────────────────
using OneOverGpuSm = effects::ConcurrentRow<effects::SmBudget<321>>;
static_assert(!FitsCog<OneOverGpuSm, CogKind::Gpu>);

}  // namespace detail::fits_cog_self_test

}  // namespace crucible::cog
