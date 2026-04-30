// ═══════════════════════════════════════════════════════════════════
// test_d25_d29_detector_headers — sentinel TU for the 5 D25-D29
// wrapper-detector headers shipped as one batch.
//
// Per CLAUDE.md memory feedback_header_only_static_assert_blind_spot:
// header-only static_asserts are unverified under the project warning
// flags unless a .cpp TU includes them.  This sentinel forces all 5
// new headers (IsHotPath / IsWait / IsMemOrder / IsProgress /
// IsAllocClass) through the full -Werror matrix and exercises each
// header's `is_*_smoke_test()` runtime witness.
//
// This is the SCAFFOLDING-MODE confirmation file — full per-detector
// audit-extended sentinel TUs (mirroring test_is_cipher_tier.cpp et al.)
// will land alongside the D30-pattern audit pass after all detectors
// are scaffolded.  This batched TU establishes the basics:
//
//   - All 5 headers compile under the project warning matrix.
//   - All 5 self-tests (in-header static_asserts) instantiate.
//   - All 5 smoke tests run at runtime + return true.
//   - Cross-detector exclusion sanity (each detector rejects the
//     others' wrapper types).
// ═══════════════════════════════════════════════════════════════════

#include <crucible/safety/IsAllocClass.h>
#include <crucible/safety/IsHotPath.h>
#include <crucible/safety/IsMemOrder.h>
#include <crucible/safety/IsProgress.h>
#include <crucible/safety/IsWait.h>

#include <cstdio>
#include <cstdlib>

namespace {

namespace extract = ::crucible::safety::extract;
namespace safety  = ::crucible::safety;

// Cross-detector exclusion sanity — each detector must reject the
// wrapper types of the other four.  All 5 wrappers share the same
// canonical shape (template<EnumNTTP, T>); a regression that
// accidentally unifies the partial specializations would be caught
// by these asserts.

using HP = safety::HotPath<safety::HotPathTier_v::Hot, int>;
using W  = safety::Wait<safety::WaitStrategy_v::SpinPause, int>;
using MO = safety::MemOrder<safety::MemOrderTag_v::SeqCst, int>;
using P  = safety::Progress<safety::ProgressClass_v::Bounded, int>;
using AC = safety::AllocClass<safety::AllocClassTag_v::Arena, int>;

static_assert( extract::is_hot_path_v<HP>);
static_assert(!extract::is_hot_path_v<W>);
static_assert(!extract::is_hot_path_v<MO>);
static_assert(!extract::is_hot_path_v<P>);
static_assert(!extract::is_hot_path_v<AC>);

static_assert( extract::is_wait_v<W>);
static_assert(!extract::is_wait_v<HP>);
static_assert(!extract::is_wait_v<MO>);
static_assert(!extract::is_wait_v<P>);
static_assert(!extract::is_wait_v<AC>);

static_assert( extract::is_mem_order_v<MO>);
static_assert(!extract::is_mem_order_v<HP>);
static_assert(!extract::is_mem_order_v<W>);
static_assert(!extract::is_mem_order_v<P>);
static_assert(!extract::is_mem_order_v<AC>);

static_assert( extract::is_progress_v<P>);
static_assert(!extract::is_progress_v<HP>);
static_assert(!extract::is_progress_v<W>);
static_assert(!extract::is_progress_v<MO>);
static_assert(!extract::is_progress_v<AC>);

static_assert( extract::is_alloc_class_v<AC>);
static_assert(!extract::is_alloc_class_v<HP>);
static_assert(!extract::is_alloc_class_v<W>);
static_assert(!extract::is_alloc_class_v<MO>);
static_assert(!extract::is_alloc_class_v<P>);

}  // namespace

int main() {
    bool ok = true;
    ok = ok && extract::is_hot_path_smoke_test();
    ok = ok && extract::is_wait_smoke_test();
    ok = ok && extract::is_mem_order_smoke_test();
    ok = ok && extract::is_progress_smoke_test();
    ok = ok && extract::is_alloc_class_smoke_test();

    if (!ok) {
        std::fprintf(stderr,
            "test_d25_d29_detector_headers: SMOKE FAIL\n");
        return EXIT_FAILURE;
    }

    std::fprintf(stderr,
        "test_d25_d29_detector_headers: 5 detector smoke tests + "
        "25 cross-exclusion static_asserts PASSED\n");
    return EXIT_SUCCESS;
}
