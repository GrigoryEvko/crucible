// Loading the observation facades is allowed to succeed only in part,
// because the privileges needed are not always present.  A load
// therefore always yields an object, and the coverage report is what
// says which facades came up.  The runtime half of this file only
// builds where the kernel support does.

#include <crucible/perf/Senses.h>

#include <cstdio>
#include <cstdlib>  // setenv
#include <type_traits>
#include <utility>

namespace {

static_assert(!std::is_copy_constructible_v<crucible::perf::Senses>,
              "The aggregate owns seven kernel objects and seven mappings, so a "
              "copy would close each of them twice.");
static_assert(std::is_move_constructible_v<crucible::perf::Senses>,
              "The aggregate must be movable, so that a factory can return one "
              "by value.");
static_assert(!std::is_copy_assignable_v<crucible::perf::Senses>);
static_assert(std::is_move_assignable_v<crucible::perf::Senses>);

static_assert(crucible::perf::SensesMask{}.any() == false, "default-constructed SensesMask selects nothing");
static_assert(crucible::perf::SensesMask::all().any() == true, "SensesMask::all() selects everything");
static_assert(crucible::perf::SensesMask::all().sense_hub == true);
static_assert(crucible::perf::SensesMask::all().sched_switch == true);
static_assert(crucible::perf::SensesMask::all().pmu_sample == true);
static_assert(crucible::perf::SensesMask::all().lock_contention == true);
static_assert(crucible::perf::SensesMask::all().syscall_latency == true);
// The two facades that read kernel type information sit alongside the
// five older ones, so that loading everything means all seven.
static_assert(crucible::perf::SensesMask::all().sched_tp_btf == true);
static_assert(crucible::perf::SensesMask::all().syscall_tp_btf == true);

static_assert(sizeof(crucible::perf::SensesMask) <= 4, "The mask is seven single-bit fields and must stay packed.  "
                                                       "Anything larger means the packing was lost.");

static_assert(crucible::perf::CoverageReport{}.attached_count() == 0,
              "default-constructed CoverageReport reports zero attachments");

// The two constants below come in through the include graph and are
// unused otherwise, which is an error under the project's warning
// flags.  Asserting their defining relation acknowledges them.
static_assert(crucible::perf::TIMELINE_MASK == 4095, "TIMELINE_MASK = TIMELINE_CAPACITY - 1; assumes power-of-two "
                                                     "capacity so slot = idx & mask is one bitwise AND");
static_assert(crucible::perf::PMU_SAMPLE_MASK == crucible::perf::PMU_SAMPLE_CAPACITY - 1,
              "PMU_SAMPLE_MASK = capacity - 1; assumes power-of-two");

}  // anonymous namespace

int main() {
#if defined(CRUCIBLE_HAVE_BPF) && CRUCIBLE_HAVE_BPF
    setenv("CRUCIBLE_PERF_QUIET", "1", /*overwrite=*/0);

    auto s = crucible::perf::Senses::load_all(::crucible::effects::testing::init());
    static_assert(std::is_same_v<decltype(s), crucible::perf::Senses>);

    // For every facade, the coverage report and the accessor must
    // agree about whether it came up.
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
        std::fprintf(stderr,
                     "Senses: attached_count out of range "
                     "(have 7 facades, got %zu)\n",
                     cov.attached_count());
        return 1;
    }

    // Loading a subset must leave every facade outside the mask
    // unloaded.
    auto sub = crucible::perf::Senses::load_subset(::crucible::effects::testing::init(), crucible::perf::SensesMask{
                                                                                             .sense_hub = true,
                                                                                             .pmu_sample = true,
                                                                                         });
    const auto sub_cov = sub.coverage();
    if (sub.sched_switch() != nullptr || sub_cov.sched_switch_attached) {
        std::fprintf(stderr, "Senses::load_subset masked off sched_switch but it's loaded\n");
        return 1;
    }
    if (sub.lock_contention() != nullptr || sub_cov.lock_contention_attached) {
        std::fprintf(stderr, "Senses::load_subset masked off lock_contention but it's loaded\n");
        return 1;
    }
    if (sub.syscall_latency() != nullptr || sub_cov.syscall_latency_attached) {
        std::fprintf(stderr, "Senses::load_subset masked off syscall_latency but it's loaded\n");
        return 1;
    }
    if (sub.sched_tp_btf() != nullptr || sub_cov.sched_tp_btf_attached) {
        std::fprintf(stderr, "Senses::load_subset masked off sched_tp_btf but it's loaded\n");
        return 1;
    }
    if (sub.syscall_tp_btf() != nullptr || sub_cov.syscall_tp_btf_attached) {
        std::fprintf(stderr, "Senses::load_subset masked off syscall_tp_btf but it's loaded\n");
        return 1;
    }
    // The two facades inside the mask may still have failed to load,
    // for want of privileges.  Success is not asserted here; the
    // coverage report is what says what happened.

    // A move transfers ownership, so the source must be left holding
    // nothing.  That does not come for free: moving an optional member
    // moves the value it contains and leaves the source engaged, so
    // the source has to be emptied explicitly.
    auto moved_into = std::move(s);
    const auto moved_from_cov = s.coverage();
    if (moved_from_cov.attached_count() != 0u) {
        std::fprintf(stderr, "Senses moved-from must report zero attachments; got %zu\n",
                     moved_from_cov.attached_count());
        return 1;
    }
    if (s.sense_hub() != nullptr) {
        std::fprintf(stderr, "Senses: moved-from sense_hub() not null\n");
        return 1;
    }
    if (s.sched_switch() != nullptr) {
        std::fprintf(stderr, "Senses: moved-from sched_switch() not null\n");
        return 1;
    }
    if (s.pmu_sample() != nullptr) {
        std::fprintf(stderr, "Senses: moved-from pmu_sample() not null\n");
        return 1;
    }
    if (s.lock_contention() != nullptr) {
        std::fprintf(stderr, "Senses: moved-from lock_contention() not null\n");
        return 1;
    }
    if (s.syscall_latency() != nullptr) {
        std::fprintf(stderr, "Senses: moved-from syscall_latency() not null\n");
        return 1;
    }
    if (s.sched_tp_btf() != nullptr) {
        std::fprintf(stderr, "Senses: moved-from sched_tp_btf() not null\n");
        return 1;
    }
    if (s.syscall_tp_btf() != nullptr) {
        std::fprintf(stderr, "Senses: moved-from syscall_tp_btf() not null\n");
        return 1;
    }

    // Move assignment owes the same guarantee as move construction,
    // and it is a separate piece of code, so it is checked separately.
    auto reloaded = crucible::perf::Senses::load_all(::crucible::effects::testing::init());
    auto assigned_into = crucible::perf::Senses::load_subset(
        ::crucible::effects::testing::init(), crucible::perf::SensesMask{});  // empty mask — target starts disengaged
    assigned_into = std::move(reloaded);
    const auto reloaded_cov = reloaded.coverage();
    if (reloaded_cov.attached_count() != 0u) {
        std::fprintf(stderr, "Senses move-assigned-from must report zero attachments; got %zu\n",
                     reloaded_cov.attached_count());
        return 1;
    }
    if (reloaded.sense_hub() != nullptr) {
        std::fprintf(stderr, "Senses: move-assigned-from sense_hub() not null\n");
        return 1;
    }
    if (reloaded.sched_switch() != nullptr) {
        std::fprintf(stderr, "Senses: move-assigned-from sched_switch() not null\n");
        return 1;
    }
    if (reloaded.pmu_sample() != nullptr) {
        std::fprintf(stderr, "Senses: move-assigned-from pmu_sample() not null\n");
        return 1;
    }
    if (reloaded.lock_contention() != nullptr) {
        std::fprintf(stderr, "Senses: move-assigned-from lock_contention() not null\n");
        return 1;
    }
    if (reloaded.syscall_latency() != nullptr) {
        std::fprintf(stderr, "Senses: move-assigned-from syscall_latency() not null\n");
        return 1;
    }
    if (reloaded.sched_tp_btf() != nullptr) {
        std::fprintf(stderr, "Senses: move-assigned-from sched_tp_btf() not null\n");
        return 1;
    }
    if (reloaded.syscall_tp_btf() != nullptr) {
        std::fprintf(stderr, "Senses: move-assigned-from syscall_tp_btf() not null\n");
        return 1;
    }

    // Assigning an object to itself must leave it intact.  Two things
    // give that here.  A self-assignment guard returns early, and even
    // without it the body is safe, because it moves the state out into
    // a temporary and back into the same address.  A body written the
    // other way round, clearing first and moving second, would destroy
    // the state before the move could rescue it, which is the
    // regression this check exists to catch.
    const auto pre_self_move_count = assigned_into.coverage().attached_count();
    auto& self_ref = assigned_into;
    self_ref = std::move(assigned_into);
    const auto post_self_move_count = assigned_into.coverage().attached_count();
    if (pre_self_move_count != post_self_move_count) {
        std::fprintf(stderr,
                     "self-assignment perturbed the state: %zu attached became "
                     "%zu, which must not change\n",
                     pre_self_move_count, post_self_move_count);
        return 1;
    }

    (void)moved_into;
    (void)assigned_into;
#endif

    std::printf("perf::Senses smoke OK\n");
    return 0;
}
