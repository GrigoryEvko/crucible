// This translation unit is built with the three cache-size macros defined
// on the command line, standing in for a machine whose caches are larger
// than the conservative floors.  The values are 64 KiB of first-level
// data cache per core, 1 MiB of second-level per core, and 32 MiB of
// last-level in total.  Every assertion below pins one of them.

#include <crucible/concurrent/SubstrateCtxFit.h>  // conservative_l*
#include <crucible/concurrent/TopologyConstexpr.h>
#include <crucible/fixy/Pipe.h>

#include <cstddef>
#include <cstdio>
#include <cstdlib>

namespace cc = crucible::concurrent;
namespace tcx = crucible::concurrent::topology_constexpr;
namespace fpipe = crucible::fixy::pipe;
namespace ftop = crucible::fixy::pipe::topology;

#if !defined(CRUCIBLE_L1D_PER_CORE_BYTES)
#error "this file must be compiled with -DCRUCIBLE_L1D_PER_CORE_BYTES=...; check the definitions on this target"
#endif
#if !defined(CRUCIBLE_L2_PER_CORE_BYTES)
#error "this file must be compiled with -DCRUCIBLE_L2_PER_CORE_BYTES=..."
#endif
#if !defined(CRUCIBLE_L3_TOTAL_BYTES)
#error "this file must be compiled with -DCRUCIBLE_L3_TOTAL_BYTES=..."
#endif

static_assert(ftop::is_l1d_overridden_v == true, "with CRUCIBLE_L1D_PER_CORE_BYTES defined, the first-level size must "
                                                 "report as overridden");
static_assert(ftop::is_l2_overridden_v == true, "with CRUCIBLE_L2_PER_CORE_BYTES defined, the second-level size must "
                                                "report as overridden");
static_assert(ftop::is_l3_overridden_v == true, "with CRUCIBLE_L3_TOTAL_BYTES defined, the last-level size must "
                                                "report as overridden");

static_assert(ftop::l1d_per_core_bytes_v == 65536ULL,
              "the first-level size must equal the value defined on the command "
              "line");
static_assert(ftop::l2_per_core_bytes_v == 1048576ULL,
              "the second-level size must equal the value defined on the command "
              "line");
static_assert(ftop::l3_total_bytes_v == 33554432ULL, "the last-level size must equal the value defined on the command "
                                                     "line");

// An override moves the pipeline's view of the machine and nothing else.
// The substrate's conservative floors stay where they are, which is the
// separation these three assertions hold in place.
static_assert(ftop::l1d_per_core_bytes_v != cc::conservative_l1d_per_core,
              "the overridden first-level size must differ from the substrate floor");
static_assert(ftop::l2_per_core_bytes_v != cc::conservative_l2_per_core,
              "the overridden second-level size must differ from the substrate floor");
static_assert(ftop::l3_total_bytes_v != cc::conservative_l3_total,
              "the overridden last-level size must differ from the substrate floor");

namespace v223_override_witness {

// Larger than the 32 KiB floor, smaller than the 64 KiB override.
struct SixtyKProbe {
    static constexpr bool inline_safe = true;
    static constexpr bool aggregate_working_set_known = true;
    static constexpr std::size_t aggregate_per_call_working_set = 60ULL * 1024ULL;
    template <std::size_t L1d, std::size_t L2 = L1d>
    static consteval bool will_run_inline_v() noexcept {
        if constexpr (!inline_safe || !aggregate_working_set_known) {
            return false;
        } else {
            return (aggregate_per_call_working_set <= L1d) || (aggregate_per_call_working_set <= L2);
        }
    }
};

// Larger than the 256 KiB floor, smaller than the 1 MiB override.
struct EightHundredKProbe {
    static constexpr bool inline_safe = true;
    static constexpr bool aggregate_working_set_known = true;
    static constexpr std::size_t aggregate_per_call_working_set = 800ULL * 1024ULL;
    template <std::size_t L1d, std::size_t L2 = L1d>
    static consteval bool will_run_inline_v() noexcept {
        if constexpr (!inline_safe || !aggregate_working_set_known) {
            return false;
        } else {
            return (aggregate_per_call_working_set <= L1d) || (aggregate_per_call_working_set <= L2);
        }
    }
};

}  // namespace v223_override_witness

static_assert(fpipe::stance::HotPathInline<v223_override_witness::SixtyKProbe>,
              "a 60 KiB probe must run inline under the 64 KiB first-level "
              "override, through the first-level clause");

static_assert(fpipe::stance::HotPathInline<v223_override_witness::EightHundredKProbe>,
              "an 800 KiB probe must run inline under the 1 MiB second-level "
              "override, through the second-level clause.  The same probe is "
              "refused at the 256 KiB floor");

int main() {
    volatile std::size_t l1d = ftop::l1d_per_core_bytes_v;
    volatile std::size_t l2 = ftop::l2_per_core_bytes_v;
    volatile std::size_t l3 = ftop::l3_total_bytes_v;
    volatile bool ovr1 = ftop::is_l1d_overridden_v;
    volatile bool ovr2 = ftop::is_l2_overridden_v;
    volatile bool ovr3 = ftop::is_l3_overridden_v;

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
