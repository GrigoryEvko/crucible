#include <crucible/perf/SchedTpBtf.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <type_traits>
#include <utility>
#include "perf_capabilities.h"
#include "test_assert.h"

namespace {

// The wire types reach this TU through a transitive include.  These layout
// assertions deliberately duplicate those of the sibling sched smoke test, so an
// include-chain refactor that breaks one fence is caught at both.
static_assert(sizeof(crucible::perf::TimelineSchedEvent) == 32,
              "TimelineSchedEvent must be 32 B (re-used by SchedTpBtf wire contract)");
static_assert(sizeof(crucible::perf::TimelineHeader) == 64,
              "TimelineHeader must be one cache line (re-used by SchedTpBtf)");
static_assert(crucible::perf::TIMELINE_CAPACITY == 4096);
static_assert(crucible::perf::TIMELINE_MASK == 4095);

static_assert(!std::is_copy_constructible_v<crucible::perf::SchedTpBtf>,
              "SchedTpBtf owns a unique BPF object + mmap; copying would "
              "double-close — the deleted copy ctor is load-bearing");
static_assert(std::is_move_constructible_v<crucible::perf::SchedTpBtf>,
              "SchedTpBtf must be movable so it can be emplaced into std::optional");

struct DummyState {};
static_assert(sizeof(crucible::perf::SchedTpBtf) == sizeof(::fixy::Option<::fixy::Box<DummyState>>),
              "SchedTpBtf must equal the size of an Option of a Box of its state.  A larger size "
              "means that a field was added");

}  // namespace

int main() {
#if defined(CRUCIBLE_HAVE_BPF) && CRUCIBLE_HAVE_BPF
    setenv("CRUCIBLE_PERF_QUIET", "1", /*overwrite=*/0);
    ::fixy::Option<crucible::perf::SchedTpBtf> loaded =
        crucible::perf::load_sched_tp_btf(::fixy::InitLoadCtx{::foundation::effects::testing::init()});
    static_assert(std::is_same_v<decltype(loaded), ::fixy::Option<crucible::perf::SchedTpBtf>>);
    if (loaded.is_none()) return crucible::test::verdict_of_no_load("perf::load_sched_tp_btf()");

    for (crucible::perf::SchedTpBtf& hub : loaded) {
        const auto attached = hub.attached_programs();
        const auto failures = hub.attach_failures();
        if (attached.value() == 0 && failures.value() == 0) {
            std::fprintf(stderr, "perf::load_sched_tp_btf() returned populated hub with "
                                 "zero programs — should have been none\n");
            return 1;
        }
        if (attached.value() > 8 || failures.value() > 8) {
            std::fprintf(stderr,
                         "perf::SchedTpBtf: counter exceeded inplace_vector "
                         "cap (attached=%zu failures=%zu)\n",
                         attached.value(), failures.value());
            return 1;
        }

        const uint64_t cs = hub.context_switches();
        const uint64_t write_idx = hub.timeline_write_index();
        const auto timeline = hub.timeline_view();
        (void)cs;
        (void)write_idx;
        if (timeline.size() != crucible::perf::TIMELINE_CAPACITY) {
            std::fprintf(stderr, "perf::SchedTpBtf::timeline_view() — expected %u, got %zu\n",
                         crucible::perf::TIMELINE_CAPACITY, timeline.size());
            return 1;
        }

        // Reading a moved-from hub is defined here and must report zeros.
        crucible::perf::SchedTpBtf moved_into = std::move(hub);
        if (hub.context_switches() != 0u || hub.timeline_write_index() != 0u || !hub.timeline_view().empty()
            || hub.attached_programs().value() != 0u || hub.attach_failures().value() != 0u) {
            std::fprintf(stderr, "perf::SchedTpBtf moved-from accessors — expected zeros\n");
            return 1;
        }
        (void)moved_into;
    }
#endif

    crucible::test::pass("perf::SchedTpBtf smoke OK\n");
    return 0;
}
