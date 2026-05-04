// Sentinel TU for crucible::perf::SyscallTpBtf — BTF-typed syscall
// latency facade (GAPS-004f, 2026-05-04).
//
// Mirrors test_perf_syscall_latency_smoke.cpp.  Same wire format
// (TimelineSyscallEvent), so the layout asserts duplicate the legacy
// smoke's checks — this is intentional: an accidental include-chain
// refactor that breaks one fence catches the other too.

#include <crucible/perf/SyscallTpBtf.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>

namespace {

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

struct DummyState{};
static_assert(sizeof(crucible::perf::SyscallTpBtf) ==
              sizeof(std::unique_ptr<DummyState>),
    "SyscallTpBtf must equal sizeof(unique_ptr<State>) — guards against "
    "field bloat");

}  // namespace

int main() {
#if defined(CRUCIBLE_HAVE_BPF) && CRUCIBLE_HAVE_BPF
    setenv("CRUCIBLE_PERF_QUIET", "1", /*overwrite=*/0);
    std::optional<crucible::perf::SyscallTpBtf> hub =
        crucible::perf::SyscallTpBtf::load(::crucible::effects::Init{});
    static_assert(std::is_same_v<
        decltype(hub),
        std::optional<crucible::perf::SyscallTpBtf>>);

    if (hub.has_value()) {
        const auto attached = hub->attached_programs();
        const auto failures = hub->attach_failures();
        // SyscallTpBtf needs BOTH sys_enter + sys_exit attached.  load()
        // returns nullopt when fewer than 2 attach, so a populated hub
        // must have ≥ 2 links.
        if (attached.value() < 2) {
            std::fprintf(stderr,
                "perf::SyscallTpBtf::load() returned populated hub with "
                "%zu programs — should have been ≥ 2 (sys_enter + sys_exit)\n",
                attached.value());
            return 1;
        }
        if (attached.value() > 8 || failures.value() > 8) {
            std::fprintf(stderr,
                "perf::SyscallTpBtf: counter exceeded cap "
                "(attached=%zu failures=%zu)\n",
                attached.value(), failures.value());
            return 1;
        }

        const uint64_t total     = hub->total_syscalls();
        const uint64_t write_idx = hub->timeline_write_index();
        const auto     timeline  = hub->timeline_view();
        (void)total; (void)write_idx;
        if (timeline.size() != crucible::perf::TIMELINE_CAPACITY) {
            std::fprintf(stderr,
                "perf::SyscallTpBtf::timeline_view() — expected %u, got %zu\n",
                crucible::perf::TIMELINE_CAPACITY, timeline.size());
            return 1;
        }

        // Moved-from defenses.
        crucible::perf::SyscallTpBtf moved_into = std::move(*hub);
        if (hub->total_syscalls() != 0u ||
            hub->timeline_write_index() != 0u ||
            !hub->timeline_view().empty() ||
            hub->attached_programs().value() != 0u ||
            hub->attach_failures().value() != 0u) {
            std::fprintf(stderr,
                "perf::SyscallTpBtf moved-from accessors — expected zeros\n");
            return 1;
        }
        (void)moved_into;
    }
#endif

    std::printf("perf::SyscallTpBtf smoke OK\n");
    return 0;
}
