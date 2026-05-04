// Sentinel TU for crucible::perf::Senses — multi-facade aggregator
// (GAPS-004y, 2026-05-04).
//
// What this test asserts:
//   1. <crucible/perf/Senses.h> reachable through the public crucible
//      include path.
//   2. Senses is move-only (deleted copy ctor is load-bearing because
//      it owns 7 BPF objects + 7 mmaps).
//   3. SensesMask::all() and SensesMask::any() work as expected.
//   4. CoverageReport::attached_count() returns 0 on empty state.
//   5. When CRUCIBLE_HAVE_BPF=1, load_all(Init{}) returns a Senses
//      object (never nullopt — partial loads are tolerated).
//   6. Coverage matches the actual loaded-or-not state of each
//      accessor (cov.sense_hub_attached XNOR (s.sense_hub() != nullptr)).
//   7. load_subset(Init{}, mask) loads only the masked subset; other
//      accessors return nullptr.
//   8. Move semantics: moving a Senses transfers ownership; moved-from
//      reports zero attachments.

#include <crucible/perf/Senses.h>

#include <cstdio>
#include <cstdlib>     // setenv
#include <type_traits>
#include <utility>

namespace {

// ── (2) Move-only sanity ──────────────────────────────────────────────

static_assert(!std::is_copy_constructible_v<crucible::perf::Senses>,
    "Senses owns 7 BPF objects + 7 mmaps; copying would double-close — "
    "the deleted copy ctor is load-bearing");
static_assert(std::is_move_constructible_v<crucible::perf::Senses>,
    "Senses must be movable so factories can return-by-value");
static_assert(!std::is_copy_assignable_v<crucible::perf::Senses>);
static_assert(std::is_move_assignable_v<crucible::perf::Senses>);

// ── (3) SensesMask compile-time properties ────────────────────────────

static_assert(crucible::perf::SensesMask{}.any() == false,
    "default-constructed SensesMask selects nothing");
static_assert(crucible::perf::SensesMask::all().any() == true,
    "SensesMask::all() selects everything");
static_assert(crucible::perf::SensesMask::all().sense_hub == true);
static_assert(crucible::perf::SensesMask::all().sched_switch == true);
static_assert(crucible::perf::SensesMask::all().pmu_sample == true);
static_assert(crucible::perf::SensesMask::all().lock_contention == true);
static_assert(crucible::perf::SensesMask::all().syscall_latency == true);
// GAPS-004f wire-in: BTF-typed parallel facades must be in
// SensesMask::all() so load_all() literally loads all 7.
static_assert(crucible::perf::SensesMask::all().sched_tp_btf == true);
static_assert(crucible::perf::SensesMask::all().syscall_tp_btf == true);

// SensesMask should fit in a single byte (7 bool bitfields per GAPS-004f).
static_assert(sizeof(crucible::perf::SensesMask) <= 4,
    "SensesMask is 7 bool : 1 bitfields (5 legacy + 2 BTF GAPS-004f); "
    "should pack into ≤ 4 B (typically 1 B); larger means a bitfield-"
    "packing regression");

// ── (4) CoverageReport empty-state ────────────────────────────────────

static_assert(crucible::perf::CoverageReport{}.attached_count() == 0,
    "default-constructed CoverageReport reports zero attachments");

// ── Suppress -Werror=unused-const-variable on TIMELINE_MASK + PMU_SAMPLE_MASK
// transitively pulled in via SchedSwitch.h / PmuSample.h.  All sibling
// smoke tests use this same pattern to acknowledge the constants.
static_assert(crucible::perf::TIMELINE_MASK == 4095,
    "TIMELINE_MASK = TIMELINE_CAPACITY - 1; assumes power-of-two "
    "capacity so slot = idx & mask is one bitwise AND");
static_assert(crucible::perf::PMU_SAMPLE_MASK ==
              crucible::perf::PMU_SAMPLE_CAPACITY - 1,
    "PMU_SAMPLE_MASK = capacity - 1; assumes power-of-two");

}  // anonymous namespace

int main() {
#if defined(CRUCIBLE_HAVE_BPF) && CRUCIBLE_HAVE_BPF
    setenv("CRUCIBLE_PERF_QUIET", "1", /*overwrite=*/0);

    // ── (5) load_all returns a Senses unconditionally — partial loads OK.
    auto s = crucible::perf::Senses::load_all(::crucible::effects::Init{});
    static_assert(std::is_same_v<decltype(s), crucible::perf::Senses>);

    // ── (6) coverage() matches accessor non-nullness for each subprogram.
    const auto cov = s.coverage();
    if ((cov.sense_hub_attached) != (s.sense_hub() != nullptr)) {
        std::fprintf(stderr,
            "Senses: coverage.sense_hub_attached (%d) disagrees with "
            "sense_hub() != nullptr (%d)\n",
            cov.sense_hub_attached, s.sense_hub() != nullptr);
        return 1;
    }
    if ((cov.sched_switch_attached) != (s.sched_switch() != nullptr)) {
        std::fprintf(stderr, "Senses: sched_switch coverage mismatch\n");
        return 1;
    }
    if ((cov.pmu_sample_attached) != (s.pmu_sample() != nullptr)) {
        std::fprintf(stderr, "Senses: pmu_sample coverage mismatch\n");
        return 1;
    }
    if ((cov.lock_contention_attached) != (s.lock_contention() != nullptr)) {
        std::fprintf(stderr, "Senses: lock_contention coverage mismatch\n");
        return 1;
    }
    if ((cov.syscall_latency_attached) != (s.syscall_latency() != nullptr)) {
        std::fprintf(stderr, "Senses: syscall_latency coverage mismatch\n");
        return 1;
    }
    if ((cov.sched_tp_btf_attached) != (s.sched_tp_btf() != nullptr)) {
        std::fprintf(stderr, "Senses: sched_tp_btf coverage mismatch\n");
        return 1;
    }
    if ((cov.syscall_tp_btf_attached) != (s.syscall_tp_btf() != nullptr)) {
        std::fprintf(stderr, "Senses: syscall_tp_btf coverage mismatch\n");
        return 1;
    }
    if (cov.attached_count() > 7) {
        std::fprintf(stderr, "Senses: attached_count out of range "
                     "(have 7 facades, got %zu)\n", cov.attached_count());
        return 1;
    }

    // ── (7) load_subset honours the mask — unmasked accessors null.
    auto sub = crucible::perf::Senses::load_subset(
        ::crucible::effects::Init{},
        crucible::perf::SensesMask{
            .sense_hub  = true,
            .pmu_sample = true,
        });
    const auto sub_cov = sub.coverage();
    if (sub.sched_switch() != nullptr || sub_cov.sched_switch_attached) {
        std::fprintf(stderr,
            "Senses::load_subset masked off sched_switch but it's loaded\n");
        return 1;
    }
    if (sub.lock_contention() != nullptr || sub_cov.lock_contention_attached) {
        std::fprintf(stderr,
            "Senses::load_subset masked off lock_contention but it's loaded\n");
        return 1;
    }
    if (sub.syscall_latency() != nullptr || sub_cov.syscall_latency_attached) {
        std::fprintf(stderr,
            "Senses::load_subset masked off syscall_latency but it's loaded\n");
        return 1;
    }
    if (sub.sched_tp_btf() != nullptr || sub_cov.sched_tp_btf_attached) {
        std::fprintf(stderr,
            "Senses::load_subset masked off sched_tp_btf but it's loaded\n");
        return 1;
    }
    if (sub.syscall_tp_btf() != nullptr || sub_cov.syscall_tp_btf_attached) {
        std::fprintf(stderr,
            "Senses::load_subset masked off syscall_tp_btf but it's loaded\n");
        return 1;
    }
    // sense_hub / pmu_sample MAY have failed to load (e.g. no CAP_BPF
    // in the test env).  We don't insist on success — coverage() is
    // what reports it.

    // ── (8) Move semantics — moved-from reports zero attachments.
    auto moved_into = std::move(s);
    const auto moved_from_cov = s.coverage();
    if (moved_from_cov.attached_count() != 0u) {
        std::fprintf(stderr,
            "Senses moved-from must report zero attachments; got %zu\n",
            moved_from_cov.attached_count());
        return 1;
    }
    if (s.sense_hub()       != nullptr) { std::fprintf(stderr, "Senses: moved-from sense_hub() not null\n");       return 1; }
    if (s.sched_switch()    != nullptr) { std::fprintf(stderr, "Senses: moved-from sched_switch() not null\n");    return 1; }
    if (s.pmu_sample()      != nullptr) { std::fprintf(stderr, "Senses: moved-from pmu_sample() not null\n");      return 1; }
    if (s.lock_contention() != nullptr) { std::fprintf(stderr, "Senses: moved-from lock_contention() not null\n"); return 1; }
    if (s.syscall_latency() != nullptr) { std::fprintf(stderr, "Senses: moved-from syscall_latency() not null\n"); return 1; }
    if (s.sched_tp_btf()    != nullptr) { std::fprintf(stderr, "Senses: moved-from sched_tp_btf() not null\n");    return 1; }
    if (s.syscall_tp_btf()  != nullptr) { std::fprintf(stderr, "Senses: moved-from syscall_tp_btf() not null\n");  return 1; }

    (void)moved_into;
#endif

    std::printf("perf::Senses smoke OK\n");
    return 0;
}
