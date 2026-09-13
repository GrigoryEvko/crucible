#include <crucible/perf/PmuSample.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>

namespace {

static_assert(sizeof(crucible::perf::PmuSampleEvent) == 32,
              "PmuSampleEvent must be 32 B = ip(8) + tid(4) + event_type(1) + "
              "_pad[3] + ts_ns(8) + _pad8(8). The trailing pad is what makes the "
              "size divide the 64-byte cache line. Without it a slot can straddle "
              "two lines, and a torn read of the second line breaks the "
              "write-ts_ns-last completion marker for that slot");

static_assert(offsetof(crucible::perf::PmuSampleEvent, ip) == 0, "ip must be at offset 0");
static_assert(offsetof(crucible::perf::PmuSampleEvent, tid) == 8, "tid must be at offset 8");
static_assert(offsetof(crucible::perf::PmuSampleEvent, event_type) == 12, "event_type must be at offset 12");
static_assert(offsetof(crucible::perf::PmuSampleEvent, ts_ns) == 16,
              "ts_ns must be at offset 16 — written last as the completion "
              "marker. A reorder lets readers see other fields turn non-zero "
              "before ts_ns lands");
static_assert(offsetof(crucible::perf::PmuSampleEvent, _pad8) == 24,
              "_pad8 must be at offset 24 — the cache-line-coresidence pad");

static_assert(sizeof(crucible::perf::PmuSampleHeader) == 64,
              "PmuSampleHeader must be exactly one cache line so the events "
              "array starts at offset 64");

// The events array starts at offset 64, right after the header, so slot N
// begins at byte 64 + 32*N.
static_assert(64 % sizeof(crucible::perf::PmuSampleEvent) == 0,
              "the 64-byte cache line must divide evenly by the event size, "
              "otherwise events straddle cache-line boundaries and become "
              "torn-readable");

static_assert(crucible::perf::PMU_SAMPLE_CAPACITY == 32768, "PMU_SAMPLE_CAPACITY mirrors the kernel-side capacity. A "
                                                            "userspace-only bump is a wire-contract violation");
static_assert(crucible::perf::PMU_SAMPLE_MASK == 32767,
              "PMU_SAMPLE_MASK = PMU_SAMPLE_CAPACITY - 1 and assumes a "
              "power-of-two capacity, so slot = idx & mask is one bitwise AND");

// These numeric values travel over the wire, so renumbering them silently
// attributes samples to the wrong event type.

static_assert(static_cast<uint8_t>(crucible::perf::PmuEventType::LlcMiss) == 2);
static_assert(static_cast<uint8_t>(crucible::perf::PmuEventType::BranchMiss) == 3);
static_assert(static_cast<uint8_t>(crucible::perf::PmuEventType::DtlbMiss) == 4);
static_assert(static_cast<uint8_t>(crucible::perf::PmuEventType::IbsOp) == 5);
static_assert(static_cast<uint8_t>(crucible::perf::PmuEventType::IbsFetch) == 6);
static_assert(static_cast<uint8_t>(crucible::perf::PmuEventType::MajorPageFault) == 7);
static_assert(static_cast<uint8_t>(crucible::perf::PmuEventType::CpuMigration) == 8);
static_assert(static_cast<uint8_t>(crucible::perf::PmuEventType::AlignmentFault) == 9);

static_assert(!std::is_copy_constructible_v<crucible::perf::PmuSample>,
              "PmuSample owns a unique BPF object, perf-event descriptors and an "
              "mmap. Copying would double-close, so the deleted copy constructor "
              "is load-bearing");

static_assert(std::is_move_constructible_v<crucible::perf::PmuSample>,
              "PmuSample must be movable so it can be emplaced into a "
              "process-wide std::optional");

struct DummyState {};
static_assert(sizeof(crucible::perf::PmuSample) == sizeof(std::unique_ptr<DummyState>),
              "PmuSample must equal sizeof(unique_ptr<State>). A larger size means "
              "a non-EBO field was added without [[no_unique_address]], or a "
              "polymorphic vptr crept in");

}  // namespace

int main() {
#if defined(CRUCIBLE_HAVE_BPF) && CRUCIBLE_HAVE_BPF
    setenv("CRUCIBLE_PERF_QUIET", "1", /*overwrite=*/0);
    std::optional<crucible::perf::PmuSample> hub =
        crucible::perf::PmuSample::load(::crucible::effects::testing::init());
    static_assert(std::is_same_v<decltype(hub), std::optional<crucible::perf::PmuSample>>);

    if (hub.has_value()) {
        const auto attached_refined = hub->attached_programs();
        const auto failures_refined = hub->attach_failures();
        static_assert(std::is_same_v<decltype(attached_refined),
                                     const crucible::safety::Refined<crucible::safety::bounded_above<8>, std::size_t>>);
        static_assert(std::is_same_v<decltype(failures_refined),
                                     const crucible::safety::Refined<crucible::safety::bounded_above<8>, std::size_t>>);
        const std::size_t attached = attached_refined.value();
        const std::size_t failures = failures_refined.value();

        // One counter per event specification, and there are eight of them.
        if (attached + failures > 8) {
            std::fprintf(stderr,
                         "perf::PmuSample: attached(%zu) + failures(%zu) > 8 "
                         "kEventSpecs entries\n",
                         attached, failures);
            return 1;
        }
        // A populated hub means at least one attachment succeeded.
        if (attached == 0) {
            std::fprintf(stderr, "perf::PmuSample::load() returned populated hub with "
                                 "zero programs — should have been std::nullopt\n");
            return 1;
        }

        // The view spans the whole ring, not just the written prefix.
        const auto view = hub->timeline_view();
        static_assert(
            std::is_same_v<decltype(view), const crucible::safety::Borrowed<const crucible::perf::PmuSampleEvent,
                                                                            crucible::perf::PmuSample>>);
        if (view.size() != crucible::perf::PMU_SAMPLE_CAPACITY) {
            std::fprintf(stderr,
                         "perf::PmuSample::timeline_view() — expected "
                         "PMU_SAMPLE_CAPACITY (%u) events, got %zu\n",
                         crucible::perf::PMU_SAMPLE_CAPACITY, view.size());
            return 1;
        }
        if (view.empty()) {
            std::fprintf(stderr, "perf::PmuSample::timeline_view() — should be "
                                 "non-empty when hub.has_value()\n");
            return 1;
        }

        const uint64_t write_idx = hub->timeline_write_index();
        static_assert(std::is_same_v<decltype(write_idx), const uint64_t>);
        (void)write_idx;

        // The pointer is the start of the events array, one header past a
        // page-aligned mapping. Its alignment cannot be checked from here
        // without reaching into the mapping, so only non-nullness is pinned.
        if (view.data() == nullptr) {
            std::fprintf(stderr, "perf::PmuSample::timeline_view() — data() is null "
                                 "when hub.has_value()\n");
            return 1;
        }

        crucible::perf::PmuSample moved_into = std::move(*hub);
        const uint64_t write_idx_after = hub->timeline_write_index();
        const auto view_after = hub->timeline_view();
        const auto attached_after = hub->attached_programs();
        const auto failures_after = hub->attach_failures();

        if (write_idx_after != 0u) {
            std::fprintf(stderr,
                         "PmuSample::timeline_write_index() on moved-from — "
                         "expected 0, got %llu\n",
                         static_cast<unsigned long long>(write_idx_after));
            return 1;
        }
        if (!view_after.empty()) {
            std::fprintf(stderr,
                         "PmuSample::timeline_view() on moved-from — expected "
                         "empty Borrowed, got size=%zu\n",
                         view_after.size());
            return 1;
        }
        if (attached_after.value() != 0u || failures_after.value() != 0u) {
            std::fprintf(stderr,
                         "PmuSample accessors on moved-from — expected 0, got "
                         "attached=%zu failures=%zu\n",
                         attached_after.value(), failures_after.value());
            return 1;
        }
        if (moved_into.attached_programs().value() != attached) {
            std::fprintf(stderr,
                         "PmuSample move semantics — recipient lost attached "
                         "count (was %zu, now %zu)\n",
                         attached, moved_into.attached_programs().value());
            return 1;
        }
    }
#endif

    std::printf("perf::PmuSample smoke OK\n");
    return 0;
}
