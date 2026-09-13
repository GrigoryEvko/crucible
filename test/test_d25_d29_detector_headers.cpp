// Sentinel TU: compiles the five detector headers under the project warning
// flags so their static_asserts run, and calls each runtime smoke test.

#include <crucible/safety/IsAllocClass.h>
#include <crucible/safety/IsHotPath.h>
#include <crucible/safety/IsMemOrder.h>
#include <crucible/safety/IsProgress.h>
#include <crucible/safety/IsWait.h>

#include <cstdio>
#include <cstdlib>

namespace {

namespace extract = ::crucible::safety::extract;
namespace safety = ::crucible::safety;

// Each detector must reject the wrapper types of the other four.  All five
// wrappers share the same shape, template<EnumNTTP, T>.  A regression that
// accidentally unified their partial specializations would be caught here.

using HP = safety::HotPath<safety::HotPathTier_v::Hot, int>;
using W = safety::Wait<safety::WaitStrategy_v::SpinPause, int>;
using MO = safety::MemOrder<safety::MemOrderTag_v::SeqCst, int>;
using P = safety::Progress<safety::ProgressClass_v::Bounded, int>;
using AC = safety::AllocClass<safety::AllocClassTag_v::Arena, int>;

static_assert(extract::is_hot_path_v<HP>);
static_assert(!extract::is_hot_path_v<W>);
static_assert(!extract::is_hot_path_v<MO>);
static_assert(!extract::is_hot_path_v<P>);
static_assert(!extract::is_hot_path_v<AC>);

static_assert(extract::is_wait_v<W>);
static_assert(!extract::is_wait_v<HP>);
static_assert(!extract::is_wait_v<MO>);
static_assert(!extract::is_wait_v<P>);
static_assert(!extract::is_wait_v<AC>);

static_assert(extract::is_mem_order_v<MO>);
static_assert(!extract::is_mem_order_v<HP>);
static_assert(!extract::is_mem_order_v<W>);
static_assert(!extract::is_mem_order_v<P>);
static_assert(!extract::is_mem_order_v<AC>);

static_assert(extract::is_progress_v<P>);
static_assert(!extract::is_progress_v<HP>);
static_assert(!extract::is_progress_v<W>);
static_assert(!extract::is_progress_v<MO>);
static_assert(!extract::is_progress_v<AC>);

static_assert(extract::is_alloc_class_v<AC>);
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
        std::fprintf(stderr, "test_d25_d29_detector_headers: SMOKE FAIL\n");
        return EXIT_FAILURE;
    }

    std::fprintf(stderr, "test_d25_d29_detector_headers: 5 detector smoke tests + "
                         "25 cross-exclusion static_asserts PASSED\n");
    return EXIT_SUCCESS;
}
