// Sentinel TU for crucible::perf::PmuSample — second per-program
// facade in the GAPS-004 series (GAPS-004c, 2026-05-04).
//
// Verifies:
//   1. Header reachable via crucible/perf/PmuSample.h.
//   2. PmuSampleEvent / PmuSampleHeader wire-contract layout
//      (sizeof + offsetof) — pinned 32 B / 64 B respectively.
//   3. PMU_SAMPLE_CAPACITY = 32768, PMU_SAMPLE_MASK = 32767.
//   4. PmuEventType ABI-stable discriminator values.
//   5. Move-only / non-copyable.
//   6. EBO sizeof — sizeof(PmuSample) == sizeof(unique_ptr<State>).
//   7. load(effects::Init{}) link-reachable; bound-check on
//      attached_programs / attach_failures Refined types.
//   8. Moved-from defenses on every accessor.

#include <crucible/perf/PmuSample.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>

namespace {

// ── (2) Wire-contract fence-posts ──────────────────────────────────

static_assert(sizeof(crucible::perf::PmuSampleEvent) == 32,
    "PmuSampleEvent must be 32 B = ip(8) + tid(4) + event_type(1) + "
    "_pad[3] + ts_ns(8) + _pad8(8); GAPS-004c (2026-05-04) added "
    "the 8 B trailing pad for cache-line-coresidence — without it, "
    "32-byte slots would straddle 64 B cache-line boundaries and "
    "torn reads on the second line would silently break the "
    "ts_ns-LAST completion-marker contract for those slots");

static_assert(offsetof(crucible::perf::PmuSampleEvent, ip) == 0,
    "ip must be at offset 0");
static_assert(offsetof(crucible::perf::PmuSampleEvent, tid) == 8,
    "tid must be at offset 8");
static_assert(offsetof(crucible::perf::PmuSampleEvent, event_type) == 12,
    "event_type must be at offset 12");
static_assert(offsetof(crucible::perf::PmuSampleEvent, ts_ns) == 16,
    "ts_ns MUST be at offset 16 — written LAST as the completion "
    "marker; a reorder would have readers seeing other fields "
    "non-zero before ts_ns lands");
static_assert(offsetof(crucible::perf::PmuSampleEvent, _pad8) == 24,
    "_pad8 must be at offset 24 — the cache-line-coresidence pad");

static_assert(sizeof(crucible::perf::PmuSampleHeader) == 64,
    "PmuSampleHeader must be exactly one cache line so the events "
    "array starts at offset 64");

// Cache-line-coresidence sanity — events array starts at offset 64;
// each slot at byte (64 + 32*N).  32 divides 64, so every slot
// fits cleanly within one 64 B line.
static_assert(64 % sizeof(crucible::perf::PmuSampleEvent) == 0,
    "Cache line size (64) must be evenly divisible by event size "
    "(32); otherwise events would straddle cache-line boundaries "
    "and reintroduce the torn-read bug GAPS-004b/c-AUDIT fixed");

// ── (3) Capacity + mask ────────────────────────────────────────────

static_assert(crucible::perf::PMU_SAMPLE_CAPACITY == 32768,
    "PMU_SAMPLE_CAPACITY mirrors PMU_SAMPLE_CAPACITY in common.h; "
    "a userspace-only bump produces wire-contract violations");
static_assert(crucible::perf::PMU_SAMPLE_MASK == 32767,
    "PMU_SAMPLE_MASK = PMU_SAMPLE_CAPACITY - 1; assumes "
    "power-of-two capacity so slot = idx & mask is one bitwise AND");

// ── (4) Discriminator value ABI ────────────────────────────────────
//
// The numeric values are wire-contract with the BPF program's
// emit_pmu_sample(ctx, etype) calls — must NOT renumber.  A reorder
// would silently mis-attribute samples to the wrong event type.

static_assert(static_cast<uint8_t>(crucible::perf::PmuEventType::LlcMiss)        == 2);
static_assert(static_cast<uint8_t>(crucible::perf::PmuEventType::BranchMiss)     == 3);
static_assert(static_cast<uint8_t>(crucible::perf::PmuEventType::DtlbMiss)       == 4);
static_assert(static_cast<uint8_t>(crucible::perf::PmuEventType::IbsOp)          == 5);
static_assert(static_cast<uint8_t>(crucible::perf::PmuEventType::IbsFetch)       == 6);
static_assert(static_cast<uint8_t>(crucible::perf::PmuEventType::MajorPageFault) == 7);
static_assert(static_cast<uint8_t>(crucible::perf::PmuEventType::CpuMigration)   == 8);
static_assert(static_cast<uint8_t>(crucible::perf::PmuEventType::AlignmentFault) == 9);

// ── (5) Move-only / (6) EBO sizeof ─────────────────────────────────

static_assert(!std::is_copy_constructible_v<crucible::perf::PmuSample>,
    "PmuSample owns a unique BPF object + perf_event FDs + mmap; "
    "copying would double-close — the deleted copy ctor is load-bearing");

static_assert(std::is_move_constructible_v<crucible::perf::PmuSample>,
    "PmuSample must be movable so it can be emplaced into a "
    "process-wide std::optional");

struct DummyState{};
static_assert(sizeof(crucible::perf::PmuSample) ==
              sizeof(std::unique_ptr<DummyState>),
    "PmuSample must equal sizeof(unique_ptr<State>); a regression "
    "here means a non-EBO field was added without [[no_unique_address]] "
    "(or a polymorphic vptr crept in)");

}  // namespace

int main() {
    // ── (7) load() reachability + populated-hub accessor sanity.
#if defined(CRUCIBLE_HAVE_BPF) && CRUCIBLE_HAVE_BPF
    setenv("CRUCIBLE_PERF_QUIET", "1", /*overwrite=*/0);
    std::optional<crucible::perf::PmuSample> hub =
        crucible::perf::PmuSample::load(::crucible::effects::Init{});
    static_assert(std::is_same_v<
        decltype(hub),
        std::optional<crucible::perf::PmuSample>>);

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

        // attached + failures ≤ 8 (we have 8 event specs); kEventSpecs.
        if (attached + failures > 8) {
            std::fprintf(stderr,
                "perf::PmuSample: attached(%zu) + failures(%zu) > 8 "
                "kEventSpecs entries\n", attached, failures);
            return 1;
        }
        // load() returned a populated hub → at least one attachment
        // succeeded.
        if (attached == 0) {
            std::fprintf(stderr,
                "perf::PmuSample::load() returned populated hub with "
                "zero programs — should have been std::nullopt\n");
            return 1;
        }

        // timeline_view returns Borrowed<const PmuSampleEvent, ...>
        // spanning PMU_SAMPLE_CAPACITY events.
        const auto view = hub->timeline_view();
        static_assert(std::is_same_v<
            decltype(view),
            const crucible::safety::Borrowed<
                const crucible::perf::PmuSampleEvent,
                crucible::perf::PmuSample>>);
        if (view.size() != crucible::perf::PMU_SAMPLE_CAPACITY) {
            std::fprintf(stderr,
                "perf::PmuSample::timeline_view() — expected "
                "PMU_SAMPLE_CAPACITY (%u) events, got %zu\n",
                crucible::perf::PMU_SAMPLE_CAPACITY, view.size());
            return 1;
        }
        if (view.empty()) {
            std::fprintf(stderr,
                "perf::PmuSample::timeline_view() — should be "
                "non-empty when hub.has_value()\n");
            return 1;
        }

        const uint64_t write_idx = hub->timeline_write_index();
        static_assert(std::is_same_v<decltype(write_idx), const uint64_t>);
        (void)write_idx;

        // Wire-contract check: timeline_view().data() must be the
        // start of the events array, which is page-aligned within
        // the mmap (mmap returns page-aligned, then we offset by
        // sizeof(PmuSampleHeader)=64).  Verify the events pointer
        // is non-null when populated.  (We can't easily check page
        // alignment of the underlying mmap from this side without
        // exposing internals; the address-non-null check is the
        // structural property that matters here.)
        if (view.data() == nullptr) {
            std::fprintf(stderr,
                "perf::PmuSample::timeline_view() — data() is null "
                "when hub.has_value()\n");
            return 1;
        }

        // ── (8) Moved-from defenses.
        crucible::perf::PmuSample moved_into = std::move(*hub);
        const uint64_t write_idx_after = hub->timeline_write_index();
        const auto     view_after      = hub->timeline_view();
        const auto     attached_after  = hub->attached_programs();
        const auto     failures_after  = hub->attach_failures();

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
                "empty Borrowed, got size=%zu\n", view_after.size());
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
