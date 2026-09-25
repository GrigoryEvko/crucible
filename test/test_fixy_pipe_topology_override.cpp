// This translation unit is built with the three cache-size macros defined
// on the command line, standing in for a machine whose caches are larger
// than the conservative floors.  The values are 64 KiB of first-level
// data cache per core, 1 MiB of second-level per core, and 32 MiB of
// last-level in total.  Every assertion below pins one of them.

#include <crucible/concurrent/_SubstrateCtxFit.h>  // conservative_l*
#include <crucible/concurrent/_TopologyConstexpr.h>

#include <cstddef>
#include <cstdio>
#include <cstdlib>

namespace cc = crucible::concurrent;
namespace tcx = crucible::concurrent::topology_constexpr;

#if !defined(CRUCIBLE_L1D_PER_CORE_BYTES)
#error "this file must be compiled with -DCRUCIBLE_L1D_PER_CORE_BYTES=...; check the definitions on this target"
#endif
#if !defined(CRUCIBLE_L2_PER_CORE_BYTES)
#error "this file must be compiled with -DCRUCIBLE_L2_PER_CORE_BYTES=..."
#endif
#if !defined(CRUCIBLE_L3_TOTAL_BYTES)
#error "this file must be compiled with -DCRUCIBLE_L3_TOTAL_BYTES=..."
#endif

static_assert(tcx::is_l1d_overridden_v == true, "with CRUCIBLE_L1D_PER_CORE_BYTES defined, the first-level size must "
                                                "report as overridden");
static_assert(tcx::is_l2_overridden_v == true, "with CRUCIBLE_L2_PER_CORE_BYTES defined, the second-level size must "
                                               "report as overridden");
static_assert(tcx::is_l3_overridden_v == true, "with CRUCIBLE_L3_TOTAL_BYTES defined, the last-level size must "
                                               "report as overridden");

static_assert(tcx::l1d_per_core_bytes_v == 65536ULL,
              "the first-level size must equal the value defined on the command "
              "line");
static_assert(tcx::l2_per_core_bytes_v == 1048576ULL,
              "the second-level size must equal the value defined on the command "
              "line");
static_assert(tcx::l3_total_bytes_v == 33554432ULL, "the last-level size must equal the value defined on the command "
                                                    "line");

// An override moves the pipeline's view of the machine and nothing else.
// The substrate's conservative floors stay where they are, which is the
// separation these three assertions hold in place.
static_assert(tcx::l1d_per_core_bytes_v != cc::conservative_l1d_per_core,
              "the overridden first-level size must differ from the substrate floor");
static_assert(tcx::l2_per_core_bytes_v != cc::conservative_l2_per_core,
              "the overridden second-level size must differ from the substrate floor");
static_assert(tcx::l3_total_bytes_v != cc::conservative_l3_total,
              "the overridden last-level size must differ from the substrate floor");

int main() {
    volatile std::size_t l1d = tcx::l1d_per_core_bytes_v;
    volatile std::size_t l2 = tcx::l2_per_core_bytes_v;
    volatile std::size_t l3 = tcx::l3_total_bytes_v;
    volatile bool ovr1 = tcx::is_l1d_overridden_v;
    volatile bool ovr2 = tcx::is_l2_overridden_v;
    volatile bool ovr3 = tcx::is_l3_overridden_v;

    if (l1d != 65536U || l2 != 1048576U || l3 != 33554432U) {
        std::fprintf(stderr,
                     "override values mismatch at runtime "
                     "(l1d=%zu l2=%zu l3=%zu); the definitions on this target drifted\n",
                     static_cast<std::size_t>(l1d), static_cast<std::size_t>(l2), static_cast<std::size_t>(l3));
        std::abort();
    }
    if (!ovr1 || !ovr2 || !ovr3) {
        std::fprintf(stderr,
                     "provenance witnesses misreport at runtime "
                     "(ovr1=%d ovr2=%d ovr3=%d); the definitions drifted\n",
                     static_cast<int>(ovr1), static_cast<int>(ovr2), static_cast<int>(ovr3));
        std::abort();
    }
    return 0;
}
