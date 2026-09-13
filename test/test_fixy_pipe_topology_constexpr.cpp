// The default cache constants pinned below are the conservative floor: the
// smallest budget any deploy host is assumed to carry. A build that widens
// them without an explicit override makes every cost-model decision derived
// from them unsound on the narrowest host in the fleet. These assertions fail
// when the floor moves.

#include <crucible/concurrent/SubstrateCtxFit.h>
#include <crucible/concurrent/TopologyConstexpr.h>
#include <crucible/fixy/Pipe.h>

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <type_traits>

namespace cc = crucible::concurrent;
namespace tcx = crucible::concurrent::topology_constexpr;
namespace fpipe = crucible::fixy::pipe;
namespace ftop = crucible::fixy::pipe::topology;

static_assert(ftop::l1d_per_core_bytes_v == tcx::l1d_per_core_bytes_v,
              "fixy::pipe::topology::l1d_per_core_bytes_v must alias the "
              "topology_constexpr value.");
static_assert(ftop::l2_per_core_bytes_v == tcx::l2_per_core_bytes_v,
              "fixy::pipe::topology::l2_per_core_bytes_v must alias the "
              "topology_constexpr value.");
static_assert(ftop::l3_total_bytes_v == tcx::l3_total_bytes_v, "fixy::pipe::topology::l3_total_bytes_v must alias the "
                                                               "topology_constexpr value.");
static_assert(ftop::is_l1d_overridden_v == tcx::is_l1d_overridden_v,
              "provenance witnesses must round-trip through the fixy::pipe::topology "
              "re-export.");
static_assert(ftop::is_l2_overridden_v == tcx::is_l2_overridden_v,
              "provenance witnesses must round-trip through the fixy::pipe::topology "
              "re-export.");
static_assert(ftop::is_l3_overridden_v == tcx::is_l3_overridden_v,
              "provenance witnesses must round-trip through the fixy::pipe::topology "
              "re-export.");

#if !defined(CRUCIBLE_L1D_PER_CORE_BYTES)
static_assert(ftop::l1d_per_core_bytes_v == cc::conservative_l1d_per_core,
              "with no override, l1d_per_core_bytes_v must equal "
              "conservative_l1d_per_core.");
static_assert(ftop::l1d_per_core_bytes_v == 32ULL * 1024ULL,
              "with no override, l1d_per_core_bytes_v must equal 32 KiB.");
#endif

#if !defined(CRUCIBLE_L2_PER_CORE_BYTES)
static_assert(ftop::l2_per_core_bytes_v == cc::conservative_l2_per_core,
              "with no override, l2_per_core_bytes_v must equal "
              "conservative_l2_per_core.");
static_assert(ftop::l2_per_core_bytes_v == 256ULL * 1024ULL,
              "with no override, l2_per_core_bytes_v must equal 256 KiB.");
#endif

#if !defined(CRUCIBLE_L3_TOTAL_BYTES)
static_assert(ftop::l3_total_bytes_v == cc::conservative_l3_total,
              "with no override, l3_total_bytes_v must equal conservative_l3_total.");
static_assert(ftop::l3_total_bytes_v == 16ULL * 1024ULL * 1024ULL,
              "with no override, l3_total_bytes_v must equal 16 MiB.");
#endif

#if !defined(CRUCIBLE_L1D_PER_CORE_BYTES)
static_assert(ftop::is_l1d_overridden_v == false, "no CRUCIBLE_L1D_PER_CORE_BYTES define implies "
                                                  "is_l1d_overridden_v == false.");
#endif
#if !defined(CRUCIBLE_L2_PER_CORE_BYTES)
static_assert(ftop::is_l2_overridden_v == false, "no CRUCIBLE_L2_PER_CORE_BYTES define implies "
                                                 "is_l2_overridden_v == false.");
#endif
#if !defined(CRUCIBLE_L3_TOTAL_BYTES)
static_assert(ftop::is_l3_overridden_v == false, "no CRUCIBLE_L3_TOTAL_BYTES define implies "
                                                 "is_l3_overridden_v == false.");
#endif

static_assert(ftop::l1d_per_core_bytes_v > 0, "l1d_per_core_bytes_v must be > 0.");
static_assert(ftop::l2_per_core_bytes_v > 0, "l2_per_core_bytes_v must be > 0.");
static_assert(ftop::l3_total_bytes_v > 0, "l3_total_bytes_v must be > 0.");
static_assert(ftop::l1d_per_core_bytes_v < ftop::l2_per_core_bytes_v, "l1d < l2 monotonicity must hold.");
static_assert(ftop::l2_per_core_bytes_v < ftop::l3_total_bytes_v, "l2 < l3 monotonicity must hold.");

namespace v223_witness {

struct TinyPipelineProbe {
    static constexpr bool inline_safe = true;
    static constexpr bool aggregate_working_set_known = true;
    static constexpr std::size_t aggregate_per_call_working_set = 12ULL * 1024ULL;
    template <std::size_t L1d, std::size_t L2 = L1d>
    static consteval bool will_run_inline_v() noexcept {
        if constexpr (!inline_safe || !aggregate_working_set_known) {
            return false;
        } else {
            return (aggregate_per_call_working_set <= L1d) || (aggregate_per_call_working_set <= L2);
        }
    }
};

// 600 KiB straddles the two thresholds under test: above the conservative
// 256 KiB L2 default, below a 1 MiB explicit budget. One probe size therefore
// witnesses both the default rejection and the override acceptance.
struct MediumPipelineProbe {
    static constexpr bool inline_safe = true;
    static constexpr bool aggregate_working_set_known = true;
    static constexpr std::size_t aggregate_per_call_working_set = 600ULL * 1024ULL;
    template <std::size_t L1d, std::size_t L2 = L1d>
    static consteval bool will_run_inline_v() noexcept {
        if constexpr (!inline_safe || !aggregate_working_set_known) {
            return false;
        } else {
            return (aggregate_per_call_working_set <= L1d) || (aggregate_per_call_working_set <= L2);
        }
    }
};

}  // namespace v223_witness

static_assert(fpipe::stance::HotPathInline<v223_witness::TinyPipelineProbe>,
              "stance::HotPathInline at the default NTTPs must hold for a "
              "12-KiB-aggregate probe.");

static_assert(!fpipe::stance::HotPathInline<v223_witness::TinyPipelineProbe,
                                            /*L1dBytes=*/8ULL * 1024ULL,
                                            /*L2Bytes=*/8ULL * 1024ULL>,
              "explicit-NTTP stance must reject a 12-KiB probe against an 8-KiB "
              "budget.");

static_assert(fpipe::stance::HotPathInline<v223_witness::MediumPipelineProbe,
                                           /*L1dBytes=*/ftop::l1d_per_core_bytes_v,
                                           /*L2Bytes=*/1024ULL * 1024ULL>,
              "a 600-KiB probe must hold against an explicit 1-MiB L2 budget.");

static_assert(!fpipe::stance::HotPathInline<v223_witness::MediumPipelineProbe>,
              "a 600-KiB probe must reject at the conservative 256-KiB L2 default.");

// Concept satisfaction is settled at build time. Nothing in this translation
// unit links the runtime cache probe, so a host whose measured caches differ
// from the compiled-in budget cannot change which of the assertions above
// hold.
static_assert(std::is_same_v<decltype(fpipe::stance::HotPathInline<v223_witness::TinyPipelineProbe>), bool>,
              "stance::HotPathInline must yield bool at consteval.");

// The constants are read once in a non-constant-evaluated context: a value
// only ever touched by static_assert can change without any translation unit
// noticing. The locals are volatile so the reads are not folded away.
int main() {
    volatile std::size_t l1d = ftop::l1d_per_core_bytes_v;
    volatile std::size_t l2 = ftop::l2_per_core_bytes_v;
    volatile std::size_t l3 = ftop::l3_total_bytes_v;
    volatile bool ovr1 = ftop::is_l1d_overridden_v;
    volatile bool ovr2 = ftop::is_l2_overridden_v;
    volatile bool ovr3 = ftop::is_l3_overridden_v;

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
