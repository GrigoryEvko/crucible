#include <crucible/perf/LockContention.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>

namespace {

static_assert(sizeof(crucible::perf::TimelineLockEvent) == 32,
              "TimelineLockEvent must be 32 B = futex_addr(8) + wait_ns(8) + "
              "tid(4) + _pad(4) + ts_ns(8). Any other size means the kernel-side "
              "program writes a layout userspace does not read, producing garbage "
              "events. The size divides the 64-byte cache line, so no slot "
              "straddles two lines.");

// The wire contract is offset-sensitive, not only size-sensitive. A reorder
// that preserves the size still breaks the reader.
static_assert(offsetof(crucible::perf::TimelineLockEvent, futex_addr) == 0,
              "futex_addr must be at offset 0 — the kernel writes it first");
static_assert(offsetof(crucible::perf::TimelineLockEvent, wait_ns) == 8,
              "wait_ns must be at offset 8 — second u64 of the first cache half");
static_assert(offsetof(crucible::perf::TimelineLockEvent, tid) == 16,
              "tid must be at offset 16 — first u32 of the third u64-aligned slot");
static_assert(offsetof(crucible::perf::TimelineLockEvent, _pad) == 20,
              "_pad must be at offset 20 — packs with tid for ts_ns alignment");
static_assert(offsetof(crucible::perf::TimelineLockEvent, ts_ns) == 24,
              "ts_ns must be at offset 24 — the kernel writes it last as the "
              "completion marker. A reorder lets readers see other fields turn "
              "non-zero before ts_ns lands");

// The events array starts at offset 64, right after the header, so slot N
// begins at byte 64 + 32*N.
static_assert(64 % sizeof(crucible::perf::TimelineLockEvent) == 0,
              "the 64-byte cache line must divide evenly by the event size, "
              "otherwise events straddle cache-line boundaries and become "
              "torn-readable");

// The timeline header is shared by every program in this family, so these
// constants describe one layout rather than this program's own.
static_assert(sizeof(crucible::perf::TimelineHeader) == 64, "TimelineHeader must be exactly one cache line");
static_assert(crucible::perf::TIMELINE_CAPACITY == 4096, "TIMELINE_CAPACITY mirrors the kernel-side capacity");
static_assert(crucible::perf::TIMELINE_MASK == 4095, "TIMELINE_MASK = TIMELINE_CAPACITY - 1 and assumes a power-of-two "
                                                     "capacity, so slot = idx & mask is one bitwise AND");

static_assert(!std::is_copy_constructible_v<crucible::perf::LockContention>,
              "LockContention owns a unique BPF object and mmap. Copying would "
              "double-close, so the deleted copy constructor is load-bearing");

static_assert(std::is_move_constructible_v<crucible::perf::LockContention>,
              "LockContention must be movable so it can be emplaced into a "
              "process-wide std::optional");

struct DummyState {};
static_assert(sizeof(crucible::perf::LockContention) == sizeof(std::unique_ptr<DummyState>),
              "LockContention must equal sizeof(unique_ptr<State>). A larger size "
              "means a non-EBO field was added without [[no_unique_address]], or a "
              "polymorphic vptr crept in");

}  // namespace

int main() {
#if defined(CRUCIBLE_HAVE_BPF) && CRUCIBLE_HAVE_BPF
    setenv("CRUCIBLE_PERF_QUIET", "1", /*overwrite=*/0);
    std::optional<crucible::perf::LockContention> hub =
        crucible::perf::LockContention::load(::crucible::effects::testing::init());
    static_assert(std::is_same_v<decltype(hub), std::optional<crucible::perf::LockContention>>);

    if (hub.has_value()) {
        // The kernel-side program carries two tracepoints, one on futex
        // entry and one on exit, and load() yields nothing unless both
        // attach. The type-level bound of 8 is the structural cap on the
        // backing container.
        const auto attached_refined = hub->attached_programs();
        const auto failures_refined = hub->attach_failures();
        static_assert(std::is_same_v<decltype(attached_refined),
                                     const crucible::safety::Refined<crucible::safety::bounded_above<8>, std::size_t>>);
        static_assert(std::is_same_v<decltype(failures_refined),
                                     const crucible::safety::Refined<crucible::safety::bounded_above<8>, std::size_t>>);
        const std::size_t attached = attached_refined.value();
        const std::size_t failures = failures_refined.value();
        // A populated hub with fewer than both tracepoints attached
        // contradicts what load() promises.
        if (attached < 2) {
            std::fprintf(stderr,
                         "perf::LockContention::load() returned populated hub "
                         "with attached=%zu (expected >= 2)\n",
                         attached);
            return 1;
        }
        if (attached > 8 || failures > 8) {
            std::fprintf(stderr,
                         "perf::LockContention: counter exceeded inplace_vector "
                         "cap (attached=%zu failures=%zu)\n",
                         attached, failures);
            return 1;
        }

        // No specific count is asserted, because the kernel need not have
        // logged a futex wait yet. The check is that the call links and
        // yields a u64.
        const uint64_t waits = hub->wait_count();
        static_assert(std::is_same_v<decltype(waits), const uint64_t>);

        // The view spans the whole ring, not just the written prefix.
        const auto timeline = hub->timeline_view();
        static_assert(
            std::is_same_v<decltype(timeline), const crucible::safety::Borrowed<const crucible::perf::TimelineLockEvent,
                                                                                crucible::perf::LockContention>>);
        if (timeline.size() != crucible::perf::TIMELINE_CAPACITY) {
            std::fprintf(stderr,
                         "perf::LockContention::timeline_view() — expected "
                         "TIMELINE_CAPACITY (%u) events, got %zu\n",
                         crucible::perf::TIMELINE_CAPACITY, timeline.size());
            return 1;
        }
        if (timeline.empty()) {
            std::fprintf(stderr, "perf::LockContention::timeline_view() — view should "
                                 "be non-empty when hub.has_value()\n");
            return 1;
        }

        // The index is whatever the kernel has accumulated, so only the
        // return type is pinned.
        const uint64_t write_idx = hub->timeline_write_index();
        static_assert(std::is_same_v<decltype(write_idx), const uint64_t>);
        (void)write_idx;

        crucible::perf::LockContention moved_into = std::move(*hub);
        const uint64_t waits_after = hub->wait_count();
        const uint64_t write_idx_after = hub->timeline_write_index();
        const auto view_after = hub->timeline_view();
        const auto attached_after = hub->attached_programs();
        const auto failures_after = hub->attach_failures();

        if (waits_after != 0u) {
            std::fprintf(stderr,
                         "perf::LockContention::wait_count() on moved-from "
                         "— expected 0, got %llu\n",
                         static_cast<unsigned long long>(waits_after));
            return 1;
        }
        if (write_idx_after != 0u) {
            std::fprintf(stderr,
                         "perf::LockContention::timeline_write_index() on "
                         "moved-from — expected 0, got %llu\n",
                         static_cast<unsigned long long>(write_idx_after));
            return 1;
        }
        if (!view_after.empty()) {
            std::fprintf(stderr,
                         "perf::LockContention::timeline_view() on moved-from "
                         "— expected empty Borrowed, got size=%zu\n",
                         view_after.size());
            return 1;
        }
        if (attached_after.value() != 0u || failures_after.value() != 0u) {
            std::fprintf(stderr,
                         "perf::LockContention::attached_programs/attach_failures "
                         "on moved-from — expected 0, got attached=%zu "
                         "failures=%zu\n",
                         attached_after.value(), failures_after.value());
            return 1;
        }
        if (moved_into.attached_programs().value() != attached) {
            std::fprintf(stderr,
                         "perf::LockContention move semantics — recipient lost "
                         "attached count (was %zu, now %zu)\n",
                         attached, moved_into.attached_programs().value());
            return 1;
        }
    }
#endif

    std::printf("perf::LockContention smoke OK\n");
    return 0;
}
