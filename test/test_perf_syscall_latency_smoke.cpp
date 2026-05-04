// Sentinel TU for crucible::perf::SyscallLatency — fourth per-program
// facade in the GAPS-004 BPF series (GAPS-004e, 2026-05-04).
//
// What this test asserts:
//   1. <crucible/perf/SyscallLatency.h> reachable through the public
//      crucible include path.
//   2. TimelineSyscallEvent struct shape matches the BPF-side wire
//      contract (32 B with explicit _pad — applied UPFRONT at ship
//      time rather than as an audit-round bug fix).
//   3. SyscallLatency is move-only (deleted copy ctor is load-bearing
//      because it owns a unique BPF object + mmap).
//   4. EBO-equivalent size — sizeof(SyscallLatency) equals
//      sizeof(unique_ptr<State>); guards against accidental field bloat.
//   5. When CRUCIBLE_HAVE_BPF=1, load() is callable and returns
//      std::optional<SyscallLatency>; a populated hub exposes the
//      attached_programs / attach_failures / total_syscalls /
//      timeline_view / timeline_write_index surface.
//   6. Moved-from defenses on every accessor — read 0 / empty
//      Borrowed / Refined{0} on a moved-from SyscallLatency.

#include <crucible/perf/SyscallLatency.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>   // setenv (POSIX)
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>

namespace {

// ── (2) Wire-contract fence-posts ──────────────────────────────────

static_assert(sizeof(crucible::perf::TimelineSyscallEvent) == 32,
    "TimelineSyscallEvent must be 32 B = duration_ns(8) + tid(4) + "
    "syscall_nr(4) + ts_ns(8) + _pad(8); a regression here means the "
    "BPF program in include/crucible/perf/bpf/syscall_latency.bpf.c "
    "writes a different layout than userspace reads, producing "
    "garbage events.  The trailing _pad was added UPFRONT at GAPS-004e "
    "ship time (rather than as an audit-round bug fix) so each slot "
    "is cache-line-coresident — eliminates the torn-read window.");

// Per-field offset asserts — wire-contract is offset-sensitive.
static_assert(offsetof(crucible::perf::TimelineSyscallEvent, duration_ns) == 0,
    "duration_ns must be at offset 0 — the BPF program writes it FIRST");
static_assert(offsetof(crucible::perf::TimelineSyscallEvent, tid) == 8,
    "tid must be at offset 8 — first u32 of the second u64-aligned slot");
static_assert(offsetof(crucible::perf::TimelineSyscallEvent, syscall_nr) == 12,
    "syscall_nr must be at offset 12 — packs with tid in the same u64 word");
static_assert(offsetof(crucible::perf::TimelineSyscallEvent, ts_ns) == 16,
    "ts_ns MUST be at offset 16 — the BPF program writes it LAST as "
    "the completion marker; a reorder would have readers seeing other "
    "fields appear non-zero before ts_ns lands");
static_assert(offsetof(crucible::perf::TimelineSyscallEvent, _pad) == 24,
    "_pad must be at offset 24 — the cache-line-coresidence pad. "
    "The BPF program does NOT write this field; it exists to make "
    "sizeof(TimelineSyscallEvent) divide 64 evenly.");

// Cache-line coresidence: events array starts at offset 64 (after
// TimelineHeader); each slot at byte (64 + 32*N).  32 divides 64 →
// each slot fits within one 64-byte cache line.
static_assert(64 % sizeof(crucible::perf::TimelineSyscallEvent) == 0,
    "Cache line size (64) must be evenly divisible by event size (32); "
    "otherwise events would straddle cache-line boundaries and "
    "reintroduce a torn-read bug");

// Re-use TimelineHeader / TIMELINE_CAPACITY / TIMELINE_MASK constants
// from SchedSwitch.h (single source of truth for the shared layout).
static_assert(sizeof(crucible::perf::TimelineHeader) == 64,
    "TimelineHeader must be exactly one cache line");
static_assert(crucible::perf::TIMELINE_CAPACITY == 4096,
    "TIMELINE_CAPACITY mirrors common.h's TIMELINE_CAPACITY");
static_assert(crucible::perf::TIMELINE_MASK == 4095,
    "TIMELINE_MASK = TIMELINE_CAPACITY - 1; assumes power-of-two "
    "capacity so slot = idx & mask is one bitwise AND");

// ── (3) + (4) Type-shape sanity ────────────────────────────────────

static_assert(!std::is_copy_constructible_v<crucible::perf::SyscallLatency>,
    "SyscallLatency owns a unique BPF object + mmap; copying would "
    "double-close — the deleted copy ctor is load-bearing");

static_assert(std::is_move_constructible_v<crucible::perf::SyscallLatency>,
    "SyscallLatency must be movable so it can be emplaced into a "
    "process-wide std::optional");

struct DummyState{};
static_assert(sizeof(crucible::perf::SyscallLatency) ==
              sizeof(std::unique_ptr<DummyState>),
    "SyscallLatency must equal sizeof(unique_ptr<State>); a regression "
    "here means a non-EBO field was added without [[no_unique_address]] "
    "(or a polymorphic vptr crept in)");

}  // namespace

int main() {
    // ── (5) load() reachability + populated-hub accessor sanity.
#if defined(CRUCIBLE_HAVE_BPF) && CRUCIBLE_HAVE_BPF
    setenv("CRUCIBLE_PERF_QUIET", "1", /*overwrite=*/0);
    std::optional<crucible::perf::SyscallLatency> hub =
        crucible::perf::SyscallLatency::load(::crucible::effects::Init{});
    static_assert(std::is_same_v<
        decltype(hub),
        std::optional<crucible::perf::SyscallLatency>>);

    if (hub.has_value()) {
        const auto attached_refined = hub->attached_programs();
        const auto failures_refined = hub->attach_failures();
        static_assert(std::is_same_v<
            decltype(attached_refined),
            const crucible::safety::Refined<
                crucible::safety::bounded_above<8>, std::size_t>>);
        static_assert(std::is_same_v<
            decltype(failures_refined),
            const crucible::safety::Refined<
                crucible::safety::bounded_above<8>, std::size_t>>);
        const std::size_t attached = attached_refined.value();
        const std::size_t failures = failures_refined.value();
        // load() insists on >= 2 attachments.  attached < 2 with
        // hub.has_value() means a regression.
        if (attached < 2) {
            std::fprintf(stderr,
                "perf::SyscallLatency::load() returned populated hub "
                "with attached=%zu (expected >= 2)\n", attached);
            return 1;
        }
        if (attached > 8 || failures > 8) {
            std::fprintf(stderr,
                "perf::SyscallLatency: counter exceeded inplace_vector "
                "cap (attached=%zu failures=%zu)\n", attached, failures);
            return 1;
        }

        // total_syscalls() is a syscall — costs ~1 µs.  We don't
        // assert a specific value, only that the call link-works
        // and returns a sensible u64.
        const uint64_t syscalls = hub->total_syscalls();
        static_assert(std::is_same_v<decltype(syscalls), const uint64_t>);

        // timeline_view returns a Borrowed span over the mmap'd ring.
        const auto timeline = hub->timeline_view();
        static_assert(std::is_same_v<
            decltype(timeline),
            const crucible::safety::Borrowed<
                const crucible::perf::TimelineSyscallEvent,
                crucible::perf::SyscallLatency>>);
        if (timeline.size() != crucible::perf::TIMELINE_CAPACITY) {
            std::fprintf(stderr,
                "perf::SyscallLatency::timeline_view() — expected "
                "TIMELINE_CAPACITY (%u) events, got %zu\n",
                crucible::perf::TIMELINE_CAPACITY, timeline.size());
            return 1;
        }
        if (timeline.empty()) {
            std::fprintf(stderr,
                "perf::SyscallLatency::timeline_view() — view should "
                "be non-empty when hub.has_value()\n");
            return 1;
        }

        const uint64_t write_idx = hub->timeline_write_index();
        static_assert(std::is_same_v<decltype(write_idx), const uint64_t>);
        (void)write_idx;

        // ── (6) Moved-from defenses on every accessor.
        crucible::perf::SyscallLatency moved_into = std::move(*hub);
        const uint64_t syscalls_after  = hub->total_syscalls();
        const uint64_t write_idx_after = hub->timeline_write_index();
        const auto     view_after      = hub->timeline_view();
        const auto     attached_after  = hub->attached_programs();
        const auto     failures_after  = hub->attach_failures();

        if (syscalls_after != 0u) {
            std::fprintf(stderr,
                "perf::SyscallLatency::total_syscalls() on moved-from "
                "— expected 0, got %llu\n",
                static_cast<unsigned long long>(syscalls_after));
            return 1;
        }
        if (write_idx_after != 0u) {
            std::fprintf(stderr,
                "perf::SyscallLatency::timeline_write_index() on "
                "moved-from — expected 0, got %llu\n",
                static_cast<unsigned long long>(write_idx_after));
            return 1;
        }
        if (!view_after.empty()) {
            std::fprintf(stderr,
                "perf::SyscallLatency::timeline_view() on moved-from "
                "— expected empty Borrowed, got size=%zu\n",
                view_after.size());
            return 1;
        }
        if (attached_after.value() != 0u || failures_after.value() != 0u) {
            std::fprintf(stderr,
                "perf::SyscallLatency::attached_programs/attach_failures "
                "on moved-from — expected 0, got attached=%zu "
                "failures=%zu\n",
                attached_after.value(), failures_after.value());
            return 1;
        }
        if (moved_into.attached_programs().value() != attached) {
            std::fprintf(stderr,
                "perf::SyscallLatency move semantics — recipient lost "
                "attached count (was %zu, now %zu)\n",
                attached, moved_into.attached_programs().value());
            return 1;
        }
    }
#endif

    std::printf("perf::SyscallLatency smoke OK\n");
    return 0;
}
