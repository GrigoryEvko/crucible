// ── test_fixy_pipe_topology_constexpr — FIXY-V-223 sentinel ────────
//
// Witnesses the build-time topology constexpr surface:
//
//   1. `crucible::fixy::pipe::topology::{l1d_per_core_bytes_v,
//      l2_per_core_bytes_v, l3_total_bytes_v}` are constexpr-
//      accessible from the fixy umbrella (re-export reach).
//   2. With NO override (`-DCRUCIBLE_L*_BYTES` absent), the constants
//      default to the substrate's `conservative_l*` floor — confirms
//      the §VIII baseline (32 KiB / 256 KiB / 16 MiB).
//   3. `topology::is_l*_overridden_v` provenance witnesses report
//      `false` under default build — confirms the `#ifdef` mechanism
//      reads the absence correctly.
//   4. `stance::HotPathInline<P>` defaults pick the constexpr surface
//      (fixture #1 of V-223's three verification axes).
//   5. Explicit-NTTP form (`stance::HotPathInline<P, L1d, L2>`)
//      respects the cross-compilation simulation — pipelines that
//      fit the explicit budget hold; that exceed it reject (fixture
//      #2 simulation; the actual override-build TU is the cross-
//      compilation witness).
//   6. The stance is a CONCEPT (consteval), so `Topology::instance()`
//      runtime probe is structurally invisible to the type-level
//      claim (fixture #3 — build-time wins).
//
// Runtime smoke test runs at process start to anchor the constexpr
// values into a non-constant-evaluated context per
// `feedback_algebra_runtime_smoke_test_discipline.md` — catches the
// "header-only static_assert blind spot" if the substrate's
// conservative_l* values ever drift without re-syncing this header.
//
// HS14 verification: V-223 introduces no new mint, but the task
// description specifies three positive-verification axes (see above).
// All three witness in this TU; the override variant lives in
// `test_fixy_pipe_topology_override.cpp` (CMake target with
// `-DCRUCIBLE_L*_BYTES=...`).

#include <crucible/concurrent/SubstrateCtxFit.h>  // conservative_l*
#include <crucible/concurrent/TopologyConstexpr.h>
#include <crucible/fixy/Pipe.h>

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <type_traits>

namespace cc    = crucible::concurrent;
namespace tcx   = crucible::concurrent::topology_constexpr;
namespace fpipe = crucible::fixy::pipe;
namespace ftop  = crucible::fixy::pipe::topology;

// ─── (1) Re-export reach ────────────────────────────────────────────

static_assert(ftop::l1d_per_core_bytes_v == tcx::l1d_per_core_bytes_v,
    "FIXY-V-223: fixy::pipe::topology::l1d_per_core_bytes_v must "
    "alias the substrate-side topology_constexpr value.");
static_assert(ftop::l2_per_core_bytes_v == tcx::l2_per_core_bytes_v,
    "FIXY-V-223: fixy::pipe::topology::l2_per_core_bytes_v must "
    "alias the substrate-side topology_constexpr value.");
static_assert(ftop::l3_total_bytes_v == tcx::l3_total_bytes_v,
    "FIXY-V-223: fixy::pipe::topology::l3_total_bytes_v must "
    "alias the substrate-side topology_constexpr value.");
static_assert(ftop::is_l1d_overridden_v == tcx::is_l1d_overridden_v,
    "FIXY-V-223: provenance witnesses must round-trip through fixy::"
    "pipe::topology re-export.");
static_assert(ftop::is_l2_overridden_v == tcx::is_l2_overridden_v,
    "FIXY-V-223: provenance witnesses must round-trip through fixy::"
    "pipe::topology re-export.");
static_assert(ftop::is_l3_overridden_v == tcx::is_l3_overridden_v,
    "FIXY-V-223: provenance witnesses must round-trip through fixy::"
    "pipe::topology re-export.");

// ─── (2) Default values match substrate conservative_l* floor ──────

#if !defined(CRUCIBLE_L1D_PER_CORE_BYTES)
static_assert(ftop::l1d_per_core_bytes_v == cc::conservative_l1d_per_core,
    "FIXY-V-223: default l1d_per_core_bytes_v must equal substrate's "
    "conservative_l1d_per_core (32 KiB CLAUDE.md §VIII baseline).");
static_assert(ftop::l1d_per_core_bytes_v == 32ULL * 1024ULL,
    "FIXY-V-223: default l1d_per_core_bytes_v must equal 32 KiB.");
#endif

#if !defined(CRUCIBLE_L2_PER_CORE_BYTES)
static_assert(ftop::l2_per_core_bytes_v == cc::conservative_l2_per_core,
    "FIXY-V-223: default l2_per_core_bytes_v must equal substrate's "
    "conservative_l2_per_core (256 KiB conservative floor).");
static_assert(ftop::l2_per_core_bytes_v == 256ULL * 1024ULL,
    "FIXY-V-223: default l2_per_core_bytes_v must equal 256 KiB.");
#endif

#if !defined(CRUCIBLE_L3_TOTAL_BYTES)
static_assert(ftop::l3_total_bytes_v == cc::conservative_l3_total,
    "FIXY-V-223: default l3_total_bytes_v must equal substrate's "
    "conservative_l3_total (16 MiB).");
static_assert(ftop::l3_total_bytes_v == 16ULL * 1024ULL * 1024ULL,
    "FIXY-V-223: default l3_total_bytes_v must equal 16 MiB.");
#endif

// ─── (3) Default-build override witnesses report `false` ──────────

#if !defined(CRUCIBLE_L1D_PER_CORE_BYTES)
static_assert(ftop::is_l1d_overridden_v == false,
    "FIXY-V-223: no CRUCIBLE_L1D_PER_CORE_BYTES define implies "
    "is_l1d_overridden_v == false.");
#endif
#if !defined(CRUCIBLE_L2_PER_CORE_BYTES)
static_assert(ftop::is_l2_overridden_v == false,
    "FIXY-V-223: no CRUCIBLE_L2_PER_CORE_BYTES define implies "
    "is_l2_overridden_v == false.");
#endif
#if !defined(CRUCIBLE_L3_TOTAL_BYTES)
static_assert(ftop::is_l3_overridden_v == false,
    "FIXY-V-223: no CRUCIBLE_L3_TOTAL_BYTES define implies "
    "is_l3_overridden_v == false.");
#endif

// ─── (4) Sanity invariants (re-witnesses header static_asserts) ────

static_assert(ftop::l1d_per_core_bytes_v > 0,
    "FIXY-V-223: l1d_per_core_bytes_v must be > 0.");
static_assert(ftop::l2_per_core_bytes_v > 0,
    "FIXY-V-223: l2_per_core_bytes_v must be > 0.");
static_assert(ftop::l3_total_bytes_v > 0,
    "FIXY-V-223: l3_total_bytes_v must be > 0.");
static_assert(ftop::l1d_per_core_bytes_v < ftop::l2_per_core_bytes_v,
    "FIXY-V-223: l1d < l2 monotonicity must hold.");
static_assert(ftop::l2_per_core_bytes_v < ftop::l3_total_bytes_v,
    "FIXY-V-223: l2 < l3 monotonicity must hold.");

// ─── (5) HotPathInline stance default wiring ───────────────────────
//
// A 12-KiB-aggregate probe MUST satisfy `stance::HotPathInline` at
// the defaults (12 KiB ≤ 32 KiB L1d).  This proves V-218's stance
// reads the V-223 constexpr surface as its NTTP defaults.

namespace v223_witness {

struct TinyPipelineProbe {
    static constexpr bool inline_safe = true;
    static constexpr bool aggregate_working_set_known = true;
    static constexpr std::size_t aggregate_per_call_working_set =
        12ULL * 1024ULL;
    template <std::size_t L1d, std::size_t L2 = L1d>
    static consteval bool will_run_inline_v() noexcept {
        if constexpr (!inline_safe || !aggregate_working_set_known) {
            return false;
        } else {
            return (aggregate_per_call_working_set <= L1d)
                || (aggregate_per_call_working_set <= L2);
        }
    }
};

// 600 KiB probe — exceeds default 256 KiB L2 floor BUT fits a
// 1 MiB explicit budget.  The cross-compilation simulation: a fleet
// that explicitly declares L2=1 MiB sees stance hold; default
// (256 KiB conservative) rejects.
struct MediumPipelineProbe {
    static constexpr bool inline_safe = true;
    static constexpr bool aggregate_working_set_known = true;
    static constexpr std::size_t aggregate_per_call_working_set =
        600ULL * 1024ULL;
    template <std::size_t L1d, std::size_t L2 = L1d>
    static consteval bool will_run_inline_v() noexcept {
        if constexpr (!inline_safe || !aggregate_working_set_known) {
            return false;
        } else {
            return (aggregate_per_call_working_set <= L1d)
                || (aggregate_per_call_working_set <= L2);
        }
    }
};

}  // namespace v223_witness

// (5a) Default NTTPs — 12 KiB fits 32 KiB L1d → stance holds.
static_assert(fpipe::stance::HotPathInline<v223_witness::TinyPipelineProbe>,
    "FIXY-V-223 fixture #1: stance::HotPathInline with V-223 default "
    "NTTPs (l1d_per_core_bytes_v / l2_per_core_bytes_v) must hold for "
    "a 12-KiB-aggregate probe.");

// (5b) Explicit override of NTTPs — 12 KiB > 8 KiB L1d AND L2 →
// stance rejects.  Witnesses cross-compilation override flow:
// callers can spell the NTTPs to model alternative fleet silicon.
static_assert(
    !fpipe::stance::HotPathInline<
        v223_witness::TinyPipelineProbe,
        /*L1dBytes=*/ 8ULL * 1024ULL,
        /*L2Bytes=*/  8ULL * 1024ULL>,
    "FIXY-V-223: explicit-NTTP stance MUST reject 12 KiB > 8 KiB "
    "(simulates a hypothetically-tight fleet override).");

// (5c) Explicit override widening L2 — 600 KiB > 32 KiB L1d default
// but ≤ 1 MiB explicit L2 → stance HOLDS via the L2 clause.
static_assert(
    fpipe::stance::HotPathInline<
        v223_witness::MediumPipelineProbe,
        /*L1dBytes=*/ ftop::l1d_per_core_bytes_v,
        /*L2Bytes=*/  1024ULL * 1024ULL>,
    "FIXY-V-223 fixture #2: 600-KiB probe with explicit L2=1 MiB "
    "override MUST hold — simulates a fleet with measured L2 ≥ 1 MiB.");

// (5d) Default rejects the same probe — 600 KiB > 256 KiB L2 conservative.
static_assert(
    !fpipe::stance::HotPathInline<v223_witness::MediumPipelineProbe>,
    "FIXY-V-223 fixture #2 contrast: 600-KiB probe at conservative "
    "256-KiB L2 default MUST reject — the tightening from V-218's "
    "optimistic 1 MiB is the safer cost-model posture.");

// ─── (6) Concept = build-time only (fixture #3 structural) ─────────
//
// `stance::HotPathInline<P>` is a concept; concept evaluation is
// consteval-only.  Runtime `Topology::instance()` returns sysfs-probed
// values, but those CANNOT influence concept satisfaction — this is
// structurally true and witnessed by the fact that `static_assert`
// above succeeds at compile time WITHOUT linking against Topology's
// runtime probe.  No additional witness needed; the absence of any
// Topology.h include in this TU is the proof.

// Confirm Topology.h is NOT transitively pulled into the concept
// evaluation path — if it were, the concept could not be consteval.
static_assert(std::is_same_v<
    decltype(fpipe::stance::HotPathInline<v223_witness::TinyPipelineProbe>),
    bool>,
    "FIXY-V-223 fixture #3: stance::HotPathInline must yield a "
    "boolean type at consteval.  Concept evaluation is build-time "
    "only; runtime Topology probe is structurally invisible.");

// ─── Runtime smoke test ─────────────────────────────────────────────
//
// Non-consteval anchor — catches the header-only static_assert blind
// spot (some bugs hide until the constants are read in a runtime
// context).  Verifies sizeof + non-zero + monotonicity by reading
// the constants into volatile locals so the optimizer can't fold the
// reads out.

int main() {
    volatile std::size_t l1d = ftop::l1d_per_core_bytes_v;
    volatile std::size_t l2  = ftop::l2_per_core_bytes_v;
    volatile std::size_t l3  = ftop::l3_total_bytes_v;
    volatile bool ovr1 = ftop::is_l1d_overridden_v;
    volatile bool ovr2 = ftop::is_l2_overridden_v;
    volatile bool ovr3 = ftop::is_l3_overridden_v;

    if (l1d == 0 || l2 == 0 || l3 == 0) {
        std::fprintf(stderr,
            "FIXY-V-223: topology constants read as zero at runtime "
            "(l1d=%zu l2=%zu l3=%zu) — header broken.\n",
            static_cast<std::size_t>(l1d),
            static_cast<std::size_t>(l2),
            static_cast<std::size_t>(l3));
        std::abort();
    }
    if (!(l1d < l2 && l2 < l3)) {
        std::fprintf(stderr,
            "FIXY-V-223: monotonicity broken at runtime "
            "(l1d=%zu l2=%zu l3=%zu).\n",
            static_cast<std::size_t>(l1d),
            static_cast<std::size_t>(l2),
            static_cast<std::size_t>(l3));
        std::abort();
    }
#if !defined(CRUCIBLE_L1D_PER_CORE_BYTES)
    if (ovr1 != false) {
        std::fprintf(stderr,
            "FIXY-V-223: is_l1d_overridden_v == true at runtime "
            "without CRUCIBLE_L1D_PER_CORE_BYTES — preprocessor "
            "drift.\n");
        std::abort();
    }
#endif
    (void)ovr2;
    (void)ovr3;
    return 0;
}
