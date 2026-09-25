// The default cache constants pinned below are the conservative floor: the
// smallest budget any deploy host is assumed to carry. A build that widens
// them without an explicit override makes every cost-model decision derived
// from them unsound on the narrowest host in the fleet. These assertions fail
// when the floor moves.

#include <crucible/concurrent/_SubstrateCtxFit.h>
#include <crucible/concurrent/_TopologyConstexpr.h>

#include <cstddef>
#include <cstdio>
#include <cstdlib>

namespace cc = crucible::concurrent;
namespace tcx = crucible::concurrent::topology_constexpr;

#if !defined(CRUCIBLE_L1D_PER_CORE_BYTES)
static_assert(tcx::l1d_per_core_bytes_v == cc::conservative_l1d_per_core,
              "with no override, l1d_per_core_bytes_v must equal "
              "conservative_l1d_per_core.");
static_assert(tcx::l1d_per_core_bytes_v == 32ULL * 1024ULL,
              "with no override, l1d_per_core_bytes_v must equal 32 KiB.");
#endif

#if !defined(CRUCIBLE_L2_PER_CORE_BYTES)
static_assert(tcx::l2_per_core_bytes_v == cc::conservative_l2_per_core,
              "with no override, l2_per_core_bytes_v must equal "
              "conservative_l2_per_core.");
static_assert(tcx::l2_per_core_bytes_v == 256ULL * 1024ULL,
              "with no override, l2_per_core_bytes_v must equal 256 KiB.");
#endif

#if !defined(CRUCIBLE_L3_TOTAL_BYTES)
static_assert(tcx::l3_total_bytes_v == cc::conservative_l3_total,
              "with no override, l3_total_bytes_v must equal conservative_l3_total.");
static_assert(tcx::l3_total_bytes_v == 4ULL * 1024ULL * 1024ULL,
              "with no override, l3_total_bytes_v must equal 4 MiB.");
#endif

#if !defined(CRUCIBLE_L1D_PER_CORE_BYTES)
static_assert(tcx::is_l1d_overridden_v == false, "no CRUCIBLE_L1D_PER_CORE_BYTES define implies "
                                                 "is_l1d_overridden_v == false.");
#endif
#if !defined(CRUCIBLE_L2_PER_CORE_BYTES)
static_assert(tcx::is_l2_overridden_v == false, "no CRUCIBLE_L2_PER_CORE_BYTES define implies "
                                                "is_l2_overridden_v == false.");
#endif
#if !defined(CRUCIBLE_L3_TOTAL_BYTES)
static_assert(tcx::is_l3_overridden_v == false, "no CRUCIBLE_L3_TOTAL_BYTES define implies "
                                                "is_l3_overridden_v == false.");
#endif

static_assert(tcx::l1d_per_core_bytes_v > 0, "l1d_per_core_bytes_v must be > 0.");
static_assert(tcx::l2_per_core_bytes_v > 0, "l2_per_core_bytes_v must be > 0.");
static_assert(tcx::l3_total_bytes_v > 0, "l3_total_bytes_v must be > 0.");
static_assert(tcx::l1d_per_core_bytes_v < tcx::l2_per_core_bytes_v, "l1d < l2 monotonicity must hold.");
static_assert(tcx::l2_per_core_bytes_v < tcx::l3_total_bytes_v, "l2 < l3 monotonicity must hold.");

// The constants are read once in a non-constant-evaluated context: a value
// only ever touched by static_assert can change without any translation unit
// noticing. The locals are volatile so the reads are not folded away.
int main() {
    volatile std::size_t l1d = tcx::l1d_per_core_bytes_v;
    volatile std::size_t l2 = tcx::l2_per_core_bytes_v;
    volatile std::size_t l3 = tcx::l3_total_bytes_v;
    volatile bool ovr1 = tcx::is_l1d_overridden_v;
    volatile bool ovr2 = tcx::is_l2_overridden_v;
    volatile bool ovr3 = tcx::is_l3_overridden_v;

    if (l1d == 0 || l2 == 0 || l3 == 0) {
        std::fprintf(stderr,
                     "topology constants read as zero at runtime "
                     "(l1d=%zu l2=%zu l3=%zu) — header broken.\n",
                     static_cast<std::size_t>(l1d), static_cast<std::size_t>(l2), static_cast<std::size_t>(l3));
        std::abort();
    }
    if (!(l1d < l2 && l2 < l3)) {
        std::fprintf(stderr,
                     "topology monotonicity broken at runtime "
                     "(l1d=%zu l2=%zu l3=%zu).\n",
                     static_cast<std::size_t>(l1d), static_cast<std::size_t>(l2), static_cast<std::size_t>(l3));
        std::abort();
    }
#if !defined(CRUCIBLE_L1D_PER_CORE_BYTES)
    if (ovr1 != false) {
        std::fprintf(stderr, "is_l1d_overridden_v == true at runtime without "
                             "CRUCIBLE_L1D_PER_CORE_BYTES — preprocessor drift.\n");
        std::abort();
    }
#endif
    (void)ovr2;
    (void)ovr3;
    return 0;
}
