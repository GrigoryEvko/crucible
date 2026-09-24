#include <crucible/perf/SchedSwitch.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>

namespace {

static_assert(sizeof(crucible::perf::TimelineSchedEvent) == 32,
              "TimelineSchedEvent must be 32 B = off_cpu_ns(8) + tid(4) + "
              "on_cpu(4) + ts_ns(8) + _pad(8). Any other size means the "
              "kernel-side program writes a layout userspace does not read, "
              "producing garbage events. The trailing pad exists so the size "
              "divides the 64-byte cache line and no slot straddles two lines.");

// The wire contract is offset-sensitive, not only size-sensitive. Swapping
// off_cpu_ns and ts_ns still yields a 32-byte struct, but the kernel would
// then write ts_ns first instead of last and the completion-marker discipline
// breaks.
static_assert(offsetof(crucible::perf::TimelineSchedEvent, off_cpu_ns) == 0,
              "off_cpu_ns must be at offset 0 — the kernel writes it first");
static_assert(offsetof(crucible::perf::TimelineSchedEvent, tid) == 8,
              "tid must be at offset 8 — second u32 of the first u64-aligned half");
static_assert(offsetof(crucible::perf::TimelineSchedEvent, on_cpu) == 12,
              "on_cpu must be at offset 12 — packs with tid in the same u64 word");
static_assert(offsetof(crucible::perf::TimelineSchedEvent, ts_ns) == 16,
              "ts_ns must be at offset 16 — the kernel writes it last as the "
              "completion marker. A reorder lets readers see other fields turn "
              "non-zero before ts_ns lands");
static_assert(offsetof(crucible::perf::TimelineSchedEvent, _pad) == 24,
              "_pad must be at offset 24. The kernel never writes this field. It "
              "exists only so sizeof(TimelineSchedEvent) divides 64 evenly.");

// The events array starts at offset 64, right after the header, so slot N
// begins at byte 64 + 32*N.
static_assert(64 % sizeof(crucible::perf::TimelineSchedEvent) == 0,
              "the 64-byte cache line must divide evenly by the event size, "
              "otherwise events straddle cache-line boundaries and become "
              "torn-readable");

static_assert(sizeof(crucible::perf::TimelineHeader) == 64,
              "TimelineHeader must be exactly one cache line. The events array "
              "starts at offset 64 and the kernel-side layout pins that");

static_assert(crucible::perf::TIMELINE_CAPACITY == 4096, "TIMELINE_CAPACITY mirrors the kernel-side capacity. A "
                                                         "userspace-only bump is a wire-contract violation");

static_assert(crucible::perf::TIMELINE_MASK == 4095, "TIMELINE_MASK = TIMELINE_CAPACITY - 1 and assumes a power-of-two "
                                                     "capacity, so slot = idx & mask is one bitwise AND");

static_assert(!std::is_copy_constructible_v<crucible::perf::SchedSwitch>,
              "SchedSwitch owns a unique BPF object and mmap. Copying would "
              "double-close, so the deleted copy constructor is load-bearing");

static_assert(std::is_move_constructible_v<crucible::perf::SchedSwitch>,
              "SchedSwitch must be movable so it can be emplaced into a "
              "process-wide std::optional");

struct DummyState {};
static_assert(sizeof(crucible::perf::SchedSwitch) == sizeof(std::unique_ptr<DummyState>),
              "SchedSwitch must equal sizeof(unique_ptr<State>). A larger size "
              "means a non-EBO field was added without [[no_unique_address]], or "
              "a polymorphic vptr crept in");

}  // namespace

int main() {
#if defined(CRUCIBLE_HAVE_BPF) && CRUCIBLE_HAVE_BPF
    setenv("CRUCIBLE_PERF_QUIET", "1", /*overwrite=*/0);
    std::optional<crucible::perf::SchedSwitch> hub =
        crucible::perf::SchedSwitch::load(::crucible::effects::testing::init());
    static_assert(std::is_same_v<decltype(hub), std::optional<crucible::perf::SchedSwitch>>);

    if (hub.has_value()) {
        // The kernel-side program carries exactly one tracepoint, so attached
        // plus failed is in [0, 1] in practice. The type-level bound of 8 is
        // the structural cap on the backing container.
        const auto attached_refined = hub->attached_programs();
        const auto failures_refined = hub->attach_failures();
        static_assert(std::is_same_v<decltype(attached_refined),
                                     const crucible::safety::Refined<crucible::safety::bounded_above<8>, std::size_t>>);
        static_assert(std::is_same_v<decltype(failures_refined),
                                     const crucible::safety::Refined<crucible::safety::bounded_above<8>, std::size_t>>);
        static_assert(
            std::is_same_v<decltype(attached_refined), const crucible::fixy::wrap::MaxBounded<8, std::size_t>>);
        static_assert(
            std::is_same_v<decltype(failures_refined), const crucible::fixy::wrap::MaxBounded<8, std::size_t>>);
        const std::size_t attached = attached_refined.value();
        const std::size_t failures = failures_refined.value();
        if (attached == 0 && failures == 0) {
            std::fprintf(stderr, "perf::SchedSwitch::load() returned populated hub "
                                 "with zero programs — should have been std::nullopt\n");
            return 1;
        }
        if (attached > 8 || failures > 8) {
            std::fprintf(stderr,
                         "perf::SchedSwitch: counter exceeded inplace_vector "
                         "cap (attached=%zu failures=%zu)\n",
                         attached, failures);
            return 1;
        }

        // No specific count is asserted, because the kernel need not have
        // logged a switch yet. The check is that the call links and yields
        // a u64.
        const uint64_t cs = hub->context_switches();
        static_assert(std::is_same_v<decltype(cs), const uint64_t>);

        // The view spans the whole ring, not just the written prefix.
        // Readers pick out valid slots from timeline_write_index() and ts_ns.
        const auto timeline = hub->timeline_view();
        static_assert(std::is_same_v<decltype(timeline),
                                     const crucible::safety::Borrowed<const crucible::perf::TimelineSchedEvent,
                                                                      crucible::perf::SchedSwitch>>);
        if (timeline.size() != crucible::perf::TIMELINE_CAPACITY) {
            std::fprintf(stderr,
                         "perf::SchedSwitch::timeline_view() — expected "
                         "TIMELINE_CAPACITY (%u) events, got %zu\n",
                         crucible::perf::TIMELINE_CAPACITY, timeline.size());
            return 1;
        }
        if (timeline.empty()) {
            std::fprintf(stderr, "perf::SchedSwitch::timeline_view() — view should be "
                                 "non-empty when hub.has_value()\n");
            return 1;
        }

        // The index is whatever the kernel has accumulated, so only the
        // return type is pinned.
        const uint64_t write_idx = hub->timeline_write_index();
        static_assert(std::is_same_v<decltype(write_idx), const uint64_t>);
        (void)write_idx;

        crucible::perf::SchedSwitch moved_into = std::move(*hub);
        const uint64_t cs_after = hub->context_switches();
        const uint64_t write_idx_after = hub->timeline_write_index();
        const auto view_after = hub->timeline_view();
        const auto attached_after = hub->attached_programs();
        const auto failures_after = hub->attach_failures();

        if (cs_after != 0u) {
            std::fprintf(stderr,
                         "perf::SchedSwitch::context_switches() on moved-from "
                         "— expected 0, got %llu\n",
                         static_cast<unsigned long long>(cs_after));
            return 1;
        }
        if (write_idx_after != 0u) {
            std::fprintf(stderr,
                         "perf::SchedSwitch::timeline_write_index() on moved-from "
                         "— expected 0, got %llu\n",
                         static_cast<unsigned long long>(write_idx_after));
            return 1;
        }
        if (!view_after.empty()) {
            std::fprintf(stderr,
                         "perf::SchedSwitch::timeline_view() on moved-from — "
                         "expected empty Borrowed, got size=%zu\n",
                         view_after.size());
            return 1;
        }
        if (attached_after.value() != 0u || failures_after.value() != 0u) {
            std::fprintf(stderr,
                         "perf::SchedSwitch::attached_programs/attach_failures "
                         "on moved-from — expected 0, got attached=%zu "
                         "failures=%zu\n",
                         attached_after.value(), failures_after.value());
            return 1;
        }
        if (moved_into.attached_programs().value() != attached) {
            std::fprintf(stderr,
                         "perf::SchedSwitch move semantics — recipient lost "
                         "attached count (was %zu, now %zu)\n",
                         attached, moved_into.attached_programs().value());
            return 1;
        }
    }
#endif

    std::printf("perf::SchedSwitch smoke OK\n");
    return 0;
}
