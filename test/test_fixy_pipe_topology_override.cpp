// ── test_fixy_pipe_topology_override — FIXY-V-223 override witness ─
//
// Compile-time override variant of `test_fixy_pipe_topology_constexpr.cpp`.
// Compiled by CMake with:
//
//   target_compile_definitions(test_fixy_pipe_topology_override PRIVATE
//       CRUCIBLE_L1D_PER_CORE_BYTES=65536      // 64 KiB (Apple M1)
//       CRUCIBLE_L2_PER_CORE_BYTES=1048576     //  1 MiB (Graviton 2)
//       CRUCIBLE_L3_TOTAL_BYTES=33554432       // 32 MiB
//   )
//
// Witnesses:
//
//   1. Override mechanism is reached: with all three `CRUCIBLE_L*_BYTES`
//      defined, `topology::is_l*_overridden_v` reports `true`.
//   2. Override values flow into the constexpr surface: the constants
//      equal exactly the values supplied via `-D`.
//   3. Cross-compilation simulation: a 600-KiB-aggregate probe that
//      defaults-rejects (256 KiB conservative L2) now HOLDS at the
//      1 MiB override.
//   4. The bigger L1d (64 KiB) accepts a 60-KiB probe that the default
//      32 KiB rejects.
//
// This is V-223 fixture #2 ("cross-compilation override compiles +
// matches expected sizes") shipped end-to-end through CMake's
// `target_compile_definitions` flow.

#include <crucible/concurrent/SubstrateCtxFit.h>  // conservative_l*
#include <crucible/concurrent/TopologyConstexpr.h>
#include <crucible/fixy/Pipe.h>

#include <cstddef>
#include <cstdio>
#include <cstdlib>

namespace cc    = crucible::concurrent;
namespace tcx   = crucible::concurrent::topology_constexpr;
namespace fpipe = crucible::fixy::pipe;
namespace ftop  = crucible::fixy::pipe::topology;

// ─── (Sanity) The override defines MUST reach this TU ─────────────
#if !defined(CRUCIBLE_L1D_PER_CORE_BYTES)
#  error "FIXY-V-223: this TU MUST be compiled with -DCRUCIBLE_L1D_PER_CORE_BYTES=... — check test/CMakeLists.txt target_compile_definitions wiring."
#endif
#if !defined(CRUCIBLE_L2_PER_CORE_BYTES)
#  error "FIXY-V-223: this TU MUST be compiled with -DCRUCIBLE_L2_PER_CORE_BYTES=..."
#endif
#if !defined(CRUCIBLE_L3_TOTAL_BYTES)
#  error "FIXY-V-223: this TU MUST be compiled with -DCRUCIBLE_L3_TOTAL_BYTES=..."
#endif

// ─── (1) Override witnesses report `true` ─────────────────────────
static_assert(ftop::is_l1d_overridden_v == true,
    "FIXY-V-223 fixture #2: with CRUCIBLE_L1D_PER_CORE_BYTES defined, "
    "is_l1d_overridden_v MUST be true.");
static_assert(ftop::is_l2_overridden_v == true,
    "FIXY-V-223 fixture #2: with CRUCIBLE_L2_PER_CORE_BYTES defined, "
    "is_l2_overridden_v MUST be true.");
static_assert(ftop::is_l3_overridden_v == true,
    "FIXY-V-223 fixture #2: with CRUCIBLE_L3_TOTAL_BYTES defined, "
    "is_l3_overridden_v MUST be true.");

// ─── (2) Override values match the build-flag values exactly ──────
static_assert(ftop::l1d_per_core_bytes_v == 65536ULL,
    "FIXY-V-223 fixture #2: l1d_per_core_bytes_v MUST equal 65536 "
    "(the -DCRUCIBLE_L1D_PER_CORE_BYTES=65536 value).");
static_assert(ftop::l2_per_core_bytes_v == 1048576ULL,
    "FIXY-V-223 fixture #2: l2_per_core_bytes_v MUST equal 1048576 "
    "(the -DCRUCIBLE_L2_PER_CORE_BYTES=1048576 value).");
static_assert(ftop::l3_total_bytes_v == 33554432ULL,
    "FIXY-V-223 fixture #2: l3_total_bytes_v MUST equal 33554432 "
    "(the -DCRUCIBLE_L3_TOTAL_BYTES=33554432 value).");

// ─── (3) Overrides DIVERGE from substrate conservative_l* ─────────
// The substrate's `conservative_*` constants stay frozen — only the
// PIPELINE-LEVEL topology constants shift.  This is the architectural
// separation V-223 deliberately introduces.
static_assert(ftop::l1d_per_core_bytes_v != cc::conservative_l1d_per_core,
    "FIXY-V-223: override l1d MUST differ from substrate floor "
    "(64 KiB vs 32 KiB).");
static_assert(ftop::l2_per_core_bytes_v != cc::conservative_l2_per_core,
    "FIXY-V-223: override l2 MUST differ from substrate floor "
    "(1 MiB vs 256 KiB).");
static_assert(ftop::l3_total_bytes_v != cc::conservative_l3_total,
    "FIXY-V-223: override l3 MUST differ from substrate floor "
    "(32 MiB vs 16 MiB).");

// ─── (4) Stance behavior shifts with the override ─────────────────

namespace v223_override_witness {

// 60-KiB probe: exceeds default 32 KiB L1d, fits 64 KiB override L1d.
struct SixtyKProbe {
    static constexpr bool inline_safe = true;
    static constexpr bool aggregate_working_set_known = true;
    static constexpr std::size_t aggregate_per_call_working_set =
        60ULL * 1024ULL;
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

// 800-KiB probe: exceeds default 256 KiB L2, fits 1 MiB override L2.
struct EightHundredKProbe {
    static constexpr bool inline_safe = true;
    static constexpr bool aggregate_working_set_known = true;
    static constexpr std::size_t aggregate_per_call_working_set =
        800ULL * 1024ULL;
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

}  // namespace v223_override_witness

// (4a) 60-KiB probe holds at OVERRIDE defaults (64 KiB L1d allows it).
static_assert(
    fpipe::stance::HotPathInline<v223_override_witness::SixtyKProbe>,
    "FIXY-V-223 fixture #2: with CRUCIBLE_L1D_PER_CORE_BYTES=65536 "
    "override, a 60-KiB probe MUST satisfy stance::HotPathInline "
    "via the L1d clause (60 KiB ≤ 64 KiB).");

// (4b) 800-KiB probe holds at OVERRIDE defaults (1 MiB L2 allows it).
static_assert(
    fpipe::stance::HotPathInline<v223_override_witness::EightHundredKProbe>,
    "FIXY-V-223 fixture #2: with CRUCIBLE_L2_PER_CORE_BYTES=1048576 "
    "override, an 800-KiB probe MUST satisfy stance::HotPathInline "
    "via the L2 clause (800 KiB ≤ 1 MiB).  Same probe at default "
    "(256 KiB conservative L2) would reject.");

// ─── Runtime smoke test ─────────────────────────────────────────────

int main() {
    volatile std::size_t l1d = ftop::l1d_per_core_bytes_v;
    volatile std::size_t l2  = ftop::l2_per_core_bytes_v;
    volatile std::size_t l3  = ftop::l3_total_bytes_v;
    volatile bool ovr1 = ftop::is_l1d_overridden_v;
    volatile bool ovr2 = ftop::is_l2_overridden_v;
    volatile bool ovr3 = ftop::is_l3_overridden_v;

    if (l1d != 65536U || l2 != 1048576U || l3 != 33554432U) {
        std::fprintf(stderr,
            "FIXY-V-223: override values mismatch at runtime "
            "(l1d=%zu l2=%zu l3=%zu) — preprocessor or wiring drift.\n",
            static_cast<std::size_t>(l1d),
            static_cast<std::size_t>(l2),
            static_cast<std::size_t>(l3));
        std::abort();
    }
    if (!ovr1 || !ovr2 || !ovr3) {
        std::fprintf(stderr,
            "FIXY-V-223: provenance witnesses misreport at runtime "
            "(ovr1=%d ovr2=%d ovr3=%d) — preprocessor drift.\n",
            static_cast<int>(ovr1),
            static_cast<int>(ovr2),
            static_cast<int>(ovr3));
        std::abort();
    }
    return 0;
}
