#pragma once

// ═══════════════════════════════════════════════════════════════════
// TopologyConstexpr — build-time cache-size constants (FIXY-V-223)
//
// The runtime `Topology` (concurrent/Topology.h) probes sysfs once at
// process startup and caches L1d/L2/L3 sizes for the cost model.
// That singleton is INVISIBLE at compile time: a band-3 site that
// wants to spell its inline-fit claim in a `requires`-clause needs
// numbers that are available DURING TEMPLATE-ARGUMENT EVALUATION,
// not at runtime after the singleton wakes.
//
// This header bridges that gap.  It exposes three `inline constexpr
// std::size_t` constants — `l1d_per_core_bytes_v`, `l2_per_core_bytes_v`,
// `l3_total_bytes_v` — whose values are FROZEN AT BUILD TIME.  They
// can be overridden per-build via CMake `-D…` flags for cross-
// compilation scenarios where the configure-host differs from the
// deploy-host; otherwise they default to the conservative substrate
// values from `concurrent/SubstrateCtxFit.h` (32 KiB / 256 KiB / 16
// MiB) which match the CLAUDE.md §VIII x86_64+aarch64 baseline.
//
// ─── Override mechanism (build-local fleet tuning) ─────────────────
//
//   cmake -DCRUCIBLE_L1D_PER_CORE_BYTES=65536        // 64 KiB L1d
//         -DCRUCIBLE_L2_PER_CORE_BYTES=1048576       //  1 MiB L2
//         -DCRUCIBLE_L3_TOTAL_BYTES=33554432         // 32 MiB L3
//         ...
//
// The override flows through `-DCRUCIBLE_*` to the preprocessor; the
// constants below take it iff defined.  Override values must be
// `std::size_t`-compatible compile-time integer expressions.
//
// CONFIGURATION DIRECTIONS (build owners pick one per fleet):
//   * Local dev / single microarch: rely on defaults (matches the
//     conservative baseline; sound on every supported target).
//   * Per-fleet tuning: declare the fleet's lowest-cache-budget host
//     and supply `-DCRUCIBLE_L1D_PER_CORE_BYTES=...` matching the
//     LOWEST value across the fleet (cost-model decisions stay sound
//     for the worst case).
//   * Cross-compile: configure-host has different cache budget than
//     deploy-host — supply the override explicitly; do NOT let CMake
//     auto-detect the configure-host's `/sys/devices/.../cache/...`
//     because that would bake the wrong silicon's number into the
//     deploy-host's binary.
//
// ─── Why duplicate the conservative_* constants ────────────────────
//
// `concurrent::conservative_l1d_per_core` and friends in
// SubstrateCtxFit.h are the SUBSTRATE's safety floor — every
// substrate (Permissioned* channels) uses them in
// `fits_in_tier_v<Footprint, T>` to gate residency claims.  Those
// values intentionally do NOT shift between builds: the substrate's
// per-call working-set bound is a STRUCTURAL property of the channel
// type, not a fleet-tuning knob.
//
// The constants in THIS header are different: they parameterize
// PIPELINE-LEVEL claims (`stance::HotPathInline<P>`), which a
// fleet operator legitimately wants to tune to their silicon.  An
// Apple-M1 fleet (128 KiB L1d) gets a different inline-fit ceiling
// than a Bergamo fleet (32 KiB L1d but 4 MiB L2 per core).
//
// Defaulting to `conservative_*` preserves correctness on every
// build that doesn't opt in; the override is for fleets that have
// measured their silicon and want a tighter bound.
//
// ─── Layering position ──────────────────────────────────────────────
//
// L0 (header-only foundation).  Depends ONLY on
// `concurrent/SubstrateCtxFit.h` for the `conservative_l*` defaults.
// No std::* outside `<cstddef>`.  Self-contained: any TU that
// `#include <crucible/concurrent/TopologyConstexpr.h>` compiles
// without further setup.
//
// Surfaces re-export at `crucible::fixy::pipe::topology::*` (Pipe.h
// V-223 block) so band-3 production sites refer to the constants
// through the fixy umbrella; the substrate-side spelling
// (`crucible::concurrent::topology_constexpr::*`) is the
// implementation surface.

#include <crucible/concurrent/SubstrateCtxFit.h>  // conservative_l1d_per_core etc.

#include <cstddef>

namespace crucible::concurrent::topology_constexpr {

// ─── L1d per-core bytes ──────────────────────────────────────────────
// CMake override: -DCRUCIBLE_L1D_PER_CORE_BYTES=<value>
// Default: conservative_l1d_per_core (32 KiB)

#ifdef CRUCIBLE_L1D_PER_CORE_BYTES
inline constexpr std::size_t l1d_per_core_bytes_v =
    static_cast<std::size_t>(CRUCIBLE_L1D_PER_CORE_BYTES);
#else
inline constexpr std::size_t l1d_per_core_bytes_v = conservative_l1d_per_core;
#endif

// ─── L2 per-core bytes ───────────────────────────────────────────────
// CMake override: -DCRUCIBLE_L2_PER_CORE_BYTES=<value>
// Default: conservative_l2_per_core (256 KiB)

#ifdef CRUCIBLE_L2_PER_CORE_BYTES
inline constexpr std::size_t l2_per_core_bytes_v =
    static_cast<std::size_t>(CRUCIBLE_L2_PER_CORE_BYTES);
#else
inline constexpr std::size_t l2_per_core_bytes_v = conservative_l2_per_core;
#endif

// ─── L3 total bytes (per socket / NUMA domain) ───────────────────────
// CMake override: -DCRUCIBLE_L3_TOTAL_BYTES=<value>
// Default: conservative_l3_total (16 MiB)

#ifdef CRUCIBLE_L3_TOTAL_BYTES
inline constexpr std::size_t l3_total_bytes_v =
    static_cast<std::size_t>(CRUCIBLE_L3_TOTAL_BYTES);
#else
inline constexpr std::size_t l3_total_bytes_v = conservative_l3_total;
#endif

// ─── Sanity invariants ───────────────────────────────────────────────
// L1 must be smaller than L2 must be smaller than L3.  Any override
// that violates this is a build-config error (almost certainly a
// typo, e.g. KB-vs-MB confusion).  Fires at preprocessor-time as a
// static_assert in this header.

static_assert(l1d_per_core_bytes_v > 0,
              "FIXY-V-223: l1d_per_core_bytes_v must be > 0 — check "
              "CRUCIBLE_L1D_PER_CORE_BYTES override value.");
static_assert(l2_per_core_bytes_v > 0,
              "FIXY-V-223: l2_per_core_bytes_v must be > 0 — check "
              "CRUCIBLE_L2_PER_CORE_BYTES override value.");
static_assert(l3_total_bytes_v > 0,
              "FIXY-V-223: l3_total_bytes_v must be > 0 — check "
              "CRUCIBLE_L3_TOTAL_BYTES override value.");
static_assert(l1d_per_core_bytes_v < l2_per_core_bytes_v,
              "FIXY-V-223: l1d_per_core_bytes_v < l2_per_core_bytes_v "
              "must hold — overrides likely confused KB/MB units.");
static_assert(l2_per_core_bytes_v < l3_total_bytes_v,
              "FIXY-V-223: l2_per_core_bytes_v < l3_total_bytes_v "
              "must hold — overrides likely confused KB/MB/GB units.");

// ─── Provenance witness ──────────────────────────────────────────────
// `is_l1d_overridden_v` etc. let diagnostics distinguish "default
// (conservative substrate value)" from "build-flag override" without
// runtime probing.  Used by V-223 verification fixtures to assert
// the override mechanism is active when expected.

#ifdef CRUCIBLE_L1D_PER_CORE_BYTES
inline constexpr bool is_l1d_overridden_v = true;
#else
inline constexpr bool is_l1d_overridden_v = false;
#endif

#ifdef CRUCIBLE_L2_PER_CORE_BYTES
inline constexpr bool is_l2_overridden_v = true;
#else
inline constexpr bool is_l2_overridden_v = false;
#endif

#ifdef CRUCIBLE_L3_TOTAL_BYTES
inline constexpr bool is_l3_overridden_v = true;
#else
inline constexpr bool is_l3_overridden_v = false;
#endif

// ─── Defaults-match-substrate witness ────────────────────────────────
// When no override is supplied, the constants must equal the
// substrate's `conservative_*` values.  Catches accidental drift if
// someone changes the substrate value without re-syncing this header.

static_assert(is_l1d_overridden_v || l1d_per_core_bytes_v == conservative_l1d_per_core,
              "FIXY-V-223: default l1d_per_core_bytes_v must equal "
              "conservative_l1d_per_core when override is absent.");
static_assert(is_l2_overridden_v || l2_per_core_bytes_v == conservative_l2_per_core,
              "FIXY-V-223: default l2_per_core_bytes_v must equal "
              "conservative_l2_per_core when override is absent.");
static_assert(is_l3_overridden_v || l3_total_bytes_v == conservative_l3_total,
              "FIXY-V-223: default l3_total_bytes_v must equal "
              "conservative_l3_total when override is absent.");

}  // namespace crucible::concurrent::topology_constexpr
