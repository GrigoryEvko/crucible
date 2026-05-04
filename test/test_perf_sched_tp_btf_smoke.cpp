// Sentinel TU for crucible::perf::SchedTpBtf — BTF-typed sched_switch
// facade (GAPS-004f, 2026-05-04).
//
// Mirrors test_perf_sched_switch_smoke.cpp.  Wire-equivalent: same
// TimelineSchedEvent struct, same ring buffer layout — the
// static_asserts here just re-verify reachability through the BTF
// facade's include path (SchedTpBtf.h transitively includes
// SchedSwitch.h for the wire types).

#include <crucible/perf/SchedTpBtf.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>

namespace {

// Reachability: TimelineSchedEvent / TimelineHeader / TIMELINE_CAPACITY
// are defined in SchedSwitch.h and re-used by SchedTpBtf.h via the
// transitive include.  These asserts pin the layout for the BTF facade
// independently of the legacy smoke (so a hypothetical refactor that
// breaks one includes-chain catches both fences).
static_assert(sizeof(crucible::perf::TimelineSchedEvent) == 32,
    "TimelineSchedEvent must be 32 B (re-used by SchedTpBtf wire contract)");
static_assert(sizeof(crucible::perf::TimelineHeader) == 64,
    "TimelineHeader must be one cache line (re-used by SchedTpBtf)");
static_assert(crucible::perf::TIMELINE_CAPACITY == 4096);
static_assert(crucible::perf::TIMELINE_MASK == 4095);

// Move-only sanity.
static_assert(!std::is_copy_constructible_v<crucible::perf::SchedTpBtf>,
    "SchedTpBtf owns a unique BPF object + mmap; copying would "
    "double-close — the deleted copy ctor is load-bearing");
static_assert(std::is_move_constructible_v<crucible::perf::SchedTpBtf>,
    "SchedTpBtf must be movable so it can be emplaced into std::optional");

struct DummyState{};
static_assert(sizeof(crucible::perf::SchedTpBtf) ==
              sizeof(std::unique_ptr<DummyState>),
    "SchedTpBtf must equal sizeof(unique_ptr<State>) — guards against "
    "field bloat");

}  // namespace

int main() {
#if defined(CRUCIBLE_HAVE_BPF) && CRUCIBLE_HAVE_BPF
    setenv("CRUCIBLE_PERF_QUIET", "1", /*overwrite=*/0);
    std::optional<crucible::perf::SchedTpBtf> hub =
        crucible::perf::SchedTpBtf::load(::crucible::effects::Init{});
    static_assert(std::is_same_v<
        decltype(hub),
        std::optional<crucible::perf::SchedTpBtf>>);

    if (hub.has_value()) {
        const auto attached = hub->attached_programs();
        const auto failures = hub->attach_failures();
        if (attached.value() == 0 && failures.value() == 0) {
            std::fprintf(stderr,
                "perf::SchedTpBtf::load() returned populated hub with "
                "zero programs — should have been std::nullopt\n");
            return 1;
        }
        if (attached.value() > 8 || failures.value() > 8) {
            std::fprintf(stderr,
                "perf::SchedTpBtf: counter exceeded inplace_vector "
                "cap (attached=%zu failures=%zu)\n",
                attached.value(), failures.value());
            return 1;
        }

        const uint64_t cs        = hub->context_switches();
        const uint64_t write_idx = hub->timeline_write_index();
        const auto     timeline  = hub->timeline_view();
        (void)cs; (void)write_idx;
        if (timeline.size() != crucible::perf::TIMELINE_CAPACITY) {
            std::fprintf(stderr,
                "perf::SchedTpBtf::timeline_view() — expected %u, got %zu\n",
                crucible::perf::TIMELINE_CAPACITY, timeline.size());
            return 1;
        }

        // Moved-from defenses.
        crucible::perf::SchedTpBtf moved_into = std::move(*hub);
        if (hub->context_switches() != 0u ||
            hub->timeline_write_index() != 0u ||
            !hub->timeline_view().empty() ||
            hub->attached_programs().value() != 0u ||
            hub->attach_failures().value() != 0u) {
            std::fprintf(stderr,
                "perf::SchedTpBtf moved-from accessors — expected zeros\n");
            return 1;
        }
        (void)moved_into;
    }
#endif

    std::printf("perf::SchedTpBtf smoke OK\n");
    return 0;
}
