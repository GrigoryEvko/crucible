// Sentinel TU for crucible::perf::SchedSwitch — first per-program
// facade in the GAPS-004 BPF series (GAPS-004b, 2026-05-04).
//
// What this test asserts:
//   1. <crucible/perf/SchedSwitch.h> is reachable through the public
//      crucible include path.
//   2. The TimelineSchedEvent / TimelineHeader struct shapes match
//      the BPF-side wire contract (24 B / 64 B respectively); these
//      static_asserts are the regression-witness that catches an
//      accidental member reorder before it produces garbage events.
//   3. TIMELINE_CAPACITY = 4096, TIMELINE_MASK = 4095 — power-of-two
//      so the modulo via mask is one bitwise AND.
//   4. SchedSwitch is move-only (deleted copy ctor is load-bearing
//      because it owns a unique BPF object + mmap).
//   5. EBO-equivalent size — sizeof(SchedSwitch) equals
//      sizeof(unique_ptr<State>); guards against accidental field
//      bloat.
//   6. When CRUCIBLE_HAVE_BPF=1, load() is callable and returns
//      std::optional<SchedSwitch>; a populated hub exposes the
//      attached_programs / attach_failures / context_switches /
//      timeline_view / timeline_write_index surface.
//   7. Moved-from defenses on every accessor — read 0 / empty
//      Borrowed / Refined{0} on a moved-from SchedSwitch.

#include <crucible/perf/SchedSwitch.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>   // setenv (POSIX)
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>

namespace {

// ── (2) + (3) Wire-contract fence-posts ────────────────────────────

static_assert(sizeof(crucible::perf::TimelineSchedEvent) == 24,
    "TimelineSchedEvent must be tight 24 B = off_cpu_ns(8) + tid(4) "
    "+ on_cpu(4) + ts_ns(8); a regression here means the BPF program "
    "in include/crucible/perf/bpf/sched_switch.bpf.c writes a "
    "different layout than userspace reads, producing garbage events");

// Per-field offset asserts — wire-contract is offset-sensitive, not
// just size-sensitive.  A reorder of off_cpu_ns vs ts_ns would still
// produce a 24-byte struct but the BPF kernel would write ts_ns
// first instead of last, breaking the completion-marker discipline.
// GAPS-004b-AUDIT (#1288 analogue) — pin the layout exactly.
static_assert(offsetof(crucible::perf::TimelineSchedEvent, off_cpu_ns) == 0,
    "off_cpu_ns must be at offset 0 — the BPF program writes it FIRST");
static_assert(offsetof(crucible::perf::TimelineSchedEvent, tid)        == 8,
    "tid must be at offset 8 — second u32 of the first u64-aligned half");
static_assert(offsetof(crucible::perf::TimelineSchedEvent, on_cpu)     == 12,
    "on_cpu must be at offset 12 — packs with tid in the same u64 word");
static_assert(offsetof(crucible::perf::TimelineSchedEvent, ts_ns)      == 16,
    "ts_ns MUST be at offset 16 — the BPF program writes it LAST as the "
    "completion marker; a reorder would have readers seeing other "
    "fields appear non-zero before ts_ns lands");

static_assert(sizeof(crucible::perf::TimelineHeader) == 64,
    "TimelineHeader must be exactly one cache line; the events "
    "array starts at offset 64 — bytecode-side common.h pins this");

static_assert(crucible::perf::TIMELINE_CAPACITY == 4096,
    "TIMELINE_CAPACITY mirrors TIMELINE_CAPACITY in common.h; a "
    "userspace-only bump produces wire-contract violations");

static_assert(crucible::perf::TIMELINE_MASK == 4095,
    "TIMELINE_MASK = TIMELINE_CAPACITY - 1; assumes power-of-two "
    "capacity so slot = idx & mask is one bitwise AND");

// ── (4) + (5) Type-shape sanity ────────────────────────────────────

static_assert(!std::is_copy_constructible_v<crucible::perf::SchedSwitch>,
    "SchedSwitch owns a unique BPF object + mmap; copying would "
    "double-close — the deleted copy ctor is load-bearing");

static_assert(std::is_move_constructible_v<crucible::perf::SchedSwitch>,
    "SchedSwitch must be movable so it can be emplaced into a "
    "process-wide std::optional");

struct DummyState{};
static_assert(sizeof(crucible::perf::SchedSwitch) ==
              sizeof(std::unique_ptr<DummyState>),
    "SchedSwitch must equal sizeof(unique_ptr<State>); a regression "
    "here means a non-EBO field was added without [[no_unique_address]] "
    "(or a polymorphic vptr crept in)");

}  // namespace

int main() {
    // ── (6) load() reachability + populated-hub accessor sanity.
#if defined(CRUCIBLE_HAVE_BPF) && CRUCIBLE_HAVE_BPF
    setenv("CRUCIBLE_PERF_QUIET", "1", /*overwrite=*/0);
    std::optional<crucible::perf::SchedSwitch> hub =
        crucible::perf::SchedSwitch::load(::crucible::effects::Init{});
    static_assert(std::is_same_v<
        decltype(hub),
        std::optional<crucible::perf::SchedSwitch>>);

    if (hub.has_value()) {
        // attached_programs / attach_failures bound at type level
        // by Refined<bounded_above<8>, size_t> — sched_switch.bpf.c
        // contains exactly one tracepoint program, so attached + failed
        // is in [0, 1] in practice and within [0, 8] structurally.
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
        if (attached == 0 && failures == 0) {
            std::fprintf(stderr,
                "perf::SchedSwitch::load() returned populated hub "
                "with zero programs — should have been std::nullopt\n");
            return 1;
        }
        if (attached > 8 || failures > 8) {
            std::fprintf(stderr,
                "perf::SchedSwitch: counter exceeded inplace_vector "
                "cap (attached=%zu failures=%zu)\n", attached, failures);
            return 1;
        }

        // context_switches() is a syscall — costs ~1 µs.  We don't
        // assert a specific value (the kernel hasn't necessarily
        // logged one yet), only that the call link-works and returns
        // a sensible u64.
        const uint64_t cs = hub->context_switches();
        static_assert(std::is_same_v<decltype(cs), const uint64_t>);

        // timeline_view returns a Borrowed span over the mmap'd
        // event ring buffer.  When hub.has_value() the view must
        // span TIMELINE_CAPACITY events (the entire ring; readers
        // identify valid slots via timeline_write_index() + ts_ns).
        const auto timeline = hub->timeline_view();
        static_assert(std::is_same_v<
            decltype(timeline),
            const crucible::safety::Borrowed<
                const crucible::perf::TimelineSchedEvent,
                crucible::perf::SchedSwitch>>);
        if (timeline.size() != crucible::perf::TIMELINE_CAPACITY) {
            std::fprintf(stderr,
                "perf::SchedSwitch::timeline_view() — expected "
                "TIMELINE_CAPACITY (%u) events, got %zu\n",
                crucible::perf::TIMELINE_CAPACITY, timeline.size());
            return 1;
        }
        if (timeline.empty()) {
            std::fprintf(stderr,
                "perf::SchedSwitch::timeline_view() — view should be "
                "non-empty when hub.has_value()\n");
            return 1;
        }

        // timeline_write_index() reads the volatile header field.
        // Initial value is 0 (or whatever the kernel has accumulated
        // by the time we read).  We assert no specific value, just
        // that the call returns a u64.
        const uint64_t write_idx = hub->timeline_write_index();
        static_assert(std::is_same_v<decltype(write_idx), const uint64_t>);
        (void)write_idx;

        // ── (7) Moved-from defenses on every accessor.
        crucible::perf::SchedSwitch moved_into = std::move(*hub);
        const uint64_t cs_after        = hub->context_switches();
        const uint64_t write_idx_after = hub->timeline_write_index();
        const auto     view_after      = hub->timeline_view();
        const auto     attached_after  = hub->attached_programs();
        const auto     failures_after  = hub->attach_failures();

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
        // Symmetric witness: the move RECIPIENT still holds the live
        // hub.
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
