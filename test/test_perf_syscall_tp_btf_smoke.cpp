#include <crucible/perf/SyscallTpBtf.h>

#include <cstdint>
#include <type_traits>
#include <utility>
#include "perf_capabilities.h"
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

namespace {

// These layout assertions deliberately duplicate those of the sibling syscall
// smoke test.  Both share the wire format, so an include-chain refactor that
// breaks one fence is caught at both.
static_assert(sizeof(crucible::perf::TimelineSyscallEvent) == 32,
              "TimelineSyscallEvent must be 32 B (re-used by SyscallTpBtf wire contract)");
static_assert(sizeof(crucible::perf::TimelineHeader) == 64,
              "TimelineHeader must be one cache line (re-used by SyscallTpBtf)");
static_assert(crucible::perf::TIMELINE_CAPACITY == 4096);
static_assert(crucible::perf::TIMELINE_MASK == 4095);

static_assert(!std::is_copy_constructible_v<crucible::perf::SyscallTpBtf>,
              "SyscallTpBtf owns a unique BPF object + mmap; copying would "
              "double-close — the deleted copy ctor is load-bearing");
static_assert(std::is_move_constructible_v<crucible::perf::SyscallTpBtf>,
              "SyscallTpBtf must be movable so it can be emplaced into std::optional");

struct DummyState {};
static_assert(sizeof(crucible::perf::SyscallTpBtf) == sizeof(::fixy::Option<::fixy::Box<DummyState>>),
              "SyscallTpBtf must equal the size of an Option of a Box of its state.  A larger size "
              "means that a field was added");

}  // namespace

int main() {
#if defined(CRUCIBLE_HAVE_BPF) && CRUCIBLE_HAVE_BPF
    ::fixy::Option<crucible::perf::SyscallTpBtf> loaded =
        crucible::perf::load_syscall_tp_btf(::fixy::InitLoadCtx{::foundation::effects::testing::init()});
    static_assert(std::is_same_v<decltype(loaded), ::fixy::Option<crucible::perf::SyscallTpBtf>>);
    if (loaded.is_none()) return crucible::test::verdict_of_no_load("perf::load_syscall_tp_btf()");

    {
        crucible::perf::SyscallTpBtf hub = std::move(loaded).expect("the test above found the hub");
        const auto attached = hub.attached_programs();
        const auto failures = hub.attach_failures();
        // The facade needs both sys_enter and sys_exit attached.  The load
        // returns none when fewer than two attach, so a populated hub
        // holds at least two links.
        if (attached.value() < 2) {
            ::fixy::report(::fixy::Sink::Err,
                           "perf::load_syscall_tp_btf() returned populated hub with "
                           "{} programs — should have been ≥ 2 (sys_enter + sys_exit)\n",
                           attached.value());
            return 1;
        }
        if (attached.value() > 8 || failures.value() > 8) {
            ::fixy::report(::fixy::Sink::Err,
                           "perf::SyscallTpBtf: counter exceeded cap "
                           "(attached={} failures={})\n",
                           attached.value(), failures.value());
            return 1;
        }

        const uint64_t total = hub.total_syscalls();
        const uint64_t write_idx = hub.timeline_write_index();
        const auto timeline = hub.timeline_view();
        (void)total;
        (void)write_idx;
        if (timeline.size() != crucible::perf::TIMELINE_CAPACITY) {
            ::fixy::report(::fixy::Sink::Err, "perf::SyscallTpBtf::timeline_view() — expected {}, got {}\n",
                           crucible::perf::TIMELINE_CAPACITY, timeline.size());
            return 1;
        }

        // Reading a moved-from hub is defined here and must report zeros.
        crucible::perf::SyscallTpBtf moved_into = std::move(hub);
        if (hub.total_syscalls() != 0u || hub.timeline_write_index() != 0u || hub.timeline_dropped() != 0u
            || hub.timeline_view().size() != 0 || hub.attached_programs().value() != 0u
            || hub.attach_failures().value() != 0u) {
            ::fixy::report(::fixy::Sink::Err, "perf::SyscallTpBtf moved-from accessors — expected zeros\n");
            return 1;
        }
        (void)moved_into;
    }
#endif

    ::fixy::check::pass(kTest, "perf::SyscallTpBtf smoke OK\n");
    return 0;
}
