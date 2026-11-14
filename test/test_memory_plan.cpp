#include <crucible/MerkleDag.h>
#include <crucible/BackgroundThread.h>
#include <foundation/effects/Effect.h>
#include "test_assert.h"
#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string_view>

#include <sys/wait.h>
#include <unistd.h>

namespace {

// The report of one child process: whether it ended on SIGABRT, and its
// standard error.
struct ChildResult {
    bool was_aborted = false;
    char report[4096]{};
};

// Runs attack in a child process, with the standard error of the child on a
// pipe.  The child ends with exit status 0 when the attack returns.
ChildResult run_in_child(void (*attack)()) {
    ChildResult result{};
    int pipe_ends[2] = {-1, -1};
    if (::pipe(pipe_ends) != 0) std::abort();
    std::fflush(stderr);
    const pid_t pid = ::fork();
    if (pid < 0) std::abort();
    if (pid == 0) {
        ::close(pipe_ends[0]);
        if (::dup2(pipe_ends[1], STDERR_FILENO) < 0) std::_Exit(3);
        attack();
        std::_Exit(0);
    }
    ::close(pipe_ends[1]);
    std::size_t used = 0;
    while (used + 1 < sizeof(result.report)) {
        const ssize_t count = ::read(pipe_ends[0], result.report + used, sizeof(result.report) - 1 - used);
        if (count > 0) {
            used += static_cast<std::size_t>(count);
        } else if (count == 0 || errno != EINTR) {
            break;
        }
    }
    ::close(pipe_ends[0]);
    int status = 0;
    if (::waitpid(pid, &status, 0) != pid) std::abort();
    result.was_aborted = WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT;
    return result;
}

// One internal slot with the given lifetime, and one internal slot with a
// lifetime that the planner accepts.
void plan_two_slots(std::uint32_t birth_op, std::uint32_t death_op) {
    auto test = ::foundation::effects::testing::test();
    crucible::BackgroundThread bt;
    ::foundation::core::FixedArray<crucible::TensorSlot, 2> slots{};
    crucible::TensorSlot& first_slot = slots.at(::foundation::core::index<0>());
    first_slot.nbytes = 1024;
    first_slot.birth_op = crucible::OpIndex{0};
    first_slot.death_op = crucible::OpIndex{2};
    first_slot.slot_id = crucible::SlotId{0};
    crucible::TensorSlot& second_slot = slots.at(::foundation::core::index<1>());
    second_slot.nbytes = 1024;
    second_slot.birth_op = crucible::OpIndex{birth_op};
    second_slot.death_op = crucible::OpIndex{death_op};
    second_slot.slot_id = crucible::SlotId{1};
    crucible::MemoryPlan plan_storage{};
    ::fixy::BumpArena sweep_arena{size_t{1} << 16};
    auto* plan = bt.compute_memory_plan(test.alloc, plan_storage, sweep_arena, slots.view());
    ::fixy::report(::fixy::Sink::Out, "  the planner returned a plan of {} bytes\n",
                   static_cast<unsigned long long>(plan->pool_bytes));
}

// The sizes of the sweep come from the largest death_op + 2.  A death_op of
// UINT32_MAX - 1 makes that sum wrap to zero.
void plan_wrapping_death() { plan_two_slots(0, UINT32_MAX - 1); }

// The none() value of OpIndex as a death.  Its death_op + 1 wraps to zero,
// so the slot does not grow the sweep, and its birth indexes past it.
void plan_death_none() { plan_two_slots(5, UINT32_MAX); }

// A birth after the death.  The sweep is sized from the deaths, so the birth
// indexes past it.
void plan_birth_after_death() { plan_two_slots(9, 1); }

// The planner rejects each slot lifetime that its sweep cannot index, and it
// stops the process with a report that names the broken rule.
void test_refuses_slot_lifetimes_outside_the_sweep() {
    struct Attack {
        const char* name;
        void (*run)();
        std::string_view refusal;
    };
    const Attack attacks[] = {
        {"a death_op that wraps the sizes", &plan_wrapping_death, "MAX_DEATH_OP"},
        {"the none() value as a death_op", &plan_death_none, "MAX_DEATH_OP"},
        {"a birth_op after the death_op", &plan_birth_after_death, "birth_op.raw() <= slot.death_op.raw()"},
    };
    for (const Attack& attack : attacks) {
        const ChildResult result = run_in_child(attack.run);
        const bool names_the_rule = std::string_view{result.report}.find(attack.refusal) != std::string_view::npos;
        if (!result.was_aborted || !names_the_rule) {
            std::fprintf(stderr, "test_memory_plan: %s: aborted=%d, report:\n%s\n", attack.name,
                         static_cast<int>(result.was_aborted), result.report);
        }
        assert(result.was_aborted);
        assert(names_the_rule);
    }
    crucible::test::pass("  test_refuses_slot_lifetimes_outside_the_sweep: PASSED\n");
}

// The cases of crucible::are_byte_ranges_disjoint.  Each case is a constant, so
// a case that fails stops the build.
using crucible::are_byte_ranges_disjoint;
using crucible::ByteRange;

template <std::size_t N>
using Ranges = ::foundation::core::FixedArray<ByteRange, N>;

// An empty View has no pair.
static_assert(are_byte_ranges_disjoint(::fixy::View<ByteRange const>{}));

constexpr Ranges<1> one_range{ByteRange{0, 100}};
static_assert(are_byte_ranges_disjoint(one_range.view()));

constexpr Ranges<1> one_empty{ByteRange{50, 50}};
static_assert(are_byte_ranges_disjoint(one_empty.view()));

// Two ranges that touch at a shared end do not overlap.
constexpr Ranges<2> touching{ByteRange{0, 64}, ByteRange{64, 128}};
static_assert(are_byte_ranges_disjoint(touching.view()));

constexpr Ranges<2> gap{ByteRange{0, 64}, ByteRange{128, 256}};
static_assert(are_byte_ranges_disjoint(gap.view()));

// The order of the ranges does not change the result.
constexpr Ranges<2> reversed{ByteRange{128, 256}, ByteRange{0, 64}};
static_assert(are_byte_ranges_disjoint(reversed.view()));

constexpr Ranges<5> packed{ByteRange{0, 64}, ByteRange{64, 192}, ByteRange{192, 256}, ByteRange{256, 320},
                           ByteRange{320, 1024}};
static_assert(are_byte_ranges_disjoint(packed.view()));

constexpr Ranges<4> with_empty{ByteRange{0, 64}, ByteRange{64, 64}, ByteRange{64, 128}, ByteRange{128, 128}};
static_assert(are_byte_ranges_disjoint(with_empty.view()));

// An empty range strictly inside a second range, in the two orders of the
// pair.  A slot of zero bytes puts an empty range there.
constexpr Ranges<2> empty_inside{ByteRange{0, 10}, ByteRange{5, 5}};
static_assert(are_byte_ranges_disjoint(empty_inside.view()));

constexpr Ranges<2> empty_inside_first{ByteRange{5, 5}, ByteRange{0, 10}};
static_assert(are_byte_ranges_disjoint(empty_inside_first.view()));

constexpr Ranges<3> zero_byte_slot_inside_live{ByteRange{0, 256}, ByteRange{128, 128}, ByteRange{256, 512}};
static_assert(are_byte_ranges_disjoint(zero_byte_slot_inside_live.view()));

// An offset that does not move past the last range.
constexpr Ranges<2> adjacent_overlap{ByteRange{0, 100}, ByteRange{50, 150}};
static_assert(!are_byte_ranges_disjoint(adjacent_overlap.view()));

// A check of the adjacent pairs alone does not find this overlap.
constexpr Ranges<4> distant_overlap{ByteRange{0, 200}, ByteRange{300, 400}, ByteRange{500, 700}, ByteRange{150, 250}};
static_assert(!are_byte_ranges_disjoint(distant_overlap.view()));

constexpr Ranges<2> contained{ByteRange{0, 1024}, ByteRange{200, 800}};
static_assert(!are_byte_ranges_disjoint(contained.view()));

constexpr Ranges<2> duplicate{ByteRange{100, 200}, ByteRange{100, 200}};
static_assert(!are_byte_ranges_disjoint(duplicate.view()));

constexpr Ranges<1> inverted{ByteRange{50, 30}};
static_assert(!are_byte_ranges_disjoint(inverted.view()));

// A malformed range refuses the set, also when each pair is disjoint.
constexpr Ranges<3> mixed_inverted{ByteRange{0, 100}, ByteRange{200, 300}, ByteRange{500, 400}};
static_assert(!are_byte_ranges_disjoint(mixed_inverted.view()));

// The same check at run time, from a value that the compiler cannot fold.
void test_are_byte_ranges_disjoint_at_run_time() {
    std::uint64_t volatile shared_end = 50;
    const Ranges<2> overlap{ByteRange{0, 100}, ByteRange{shared_end, 150}};
    assert(!are_byte_ranges_disjoint(overlap.view()));
    const Ranges<2> touching_at_run_time{ByteRange{0, shared_end}, ByteRange{shared_end, 150}};
    assert(are_byte_ranges_disjoint(touching_at_run_time.view()));
    crucible::test::pass("  test_are_byte_ranges_disjoint_at_run_time: PASSED\n");
}

}  // namespace

int main() {
    test_refuses_slot_lifetimes_outside_the_sweep();
    test_are_byte_ranges_disjoint_at_run_time();
    auto test = ::foundation::effects::testing::test();
    crucible::BackgroundThread bt;

    // The intervals are chosen so slot 2 can reuse slot 1's space: slot 1 dies at
    // op 3 and slot 2 is born at op 4.  Slot 3 is external and takes no pool
    // space.
    constexpr uint32_t N = 4;
    ::foundation::core::FixedArray<crucible::TensorSlot, N> slots{};

    using crucible::SlotId;
    using crucible::OpIndex;
    using crucible::ScalarType;
    using crucible::DeviceType;
    using crucible::Layout;
    slots.at(::foundation::core::index<0>()) = {.offset_bytes = 0,
                                                .nbytes = 1024,
                                                .birth_op = OpIndex{0},
                                                .death_op = OpIndex{5},
                                                .dtype = ScalarType::Float,
                                                .device_type = DeviceType::CPU,
                                                .device_idx = 0,
                                                .layout = Layout::Strided,
                                                .is_external = false,
                                                .pad = {},
                                                .slot_id = SlotId{0},
                                                .pad2 = {}};
    slots.at(::foundation::core::index<1>()) = {.offset_bytes = 0,
                                                .nbytes = 2048,
                                                .birth_op = OpIndex{1},
                                                .death_op = OpIndex{3},
                                                .dtype = ScalarType::Float,
                                                .device_type = DeviceType::CPU,
                                                .device_idx = 0,
                                                .layout = Layout::Strided,
                                                .is_external = false,
                                                .pad = {},
                                                .slot_id = SlotId{1},
                                                .pad2 = {}};
    slots.at(::foundation::core::index<2>()) = {.offset_bytes = 0,
                                                .nbytes = 1024,
                                                .birth_op = OpIndex{4},
                                                .death_op = OpIndex{7},
                                                .dtype = ScalarType::Float,
                                                .device_type = DeviceType::CPU,
                                                .device_idx = 0,
                                                .layout = Layout::Strided,
                                                .is_external = false,
                                                .pad = {},
                                                .slot_id = SlotId{2},
                                                .pad2 = {}};
    slots.at(::foundation::core::index<3>()) = {.offset_bytes = 0,
                                                .nbytes = 512,
                                                .birth_op = OpIndex{0},
                                                .death_op = OpIndex{7},
                                                .dtype = ScalarType::Float,
                                                .device_type = DeviceType::CPU,
                                                .device_idx = 0,
                                                .layout = Layout::Strided,
                                                .is_external = true,
                                                .pad = {},
                                                .slot_id = SlotId{3},
                                                .pad2 = {}};

    crucible::MemoryPlan plan_storage{};
    ::fixy::BumpArena sweep_arena{size_t{1} << 16};
    auto* plan = bt.compute_memory_plan(test.alloc, plan_storage, sweep_arena, slots.view());
    assert(plan != nullptr);
    assert(plan->num_slots() == N);
    assert(plan->num_external == 1);
    assert(plan->pool_bytes > 0);

    assert(slots.at(::foundation::core::index<2>()).offset_bytes
           <= slots.at(::foundation::core::index<1>()).offset_bytes + 2048);

    uint64_t total_internal = 1024 + 2048 + 1024;
    assert(plan->pool_bytes < total_internal);

    // At every op boundary the simultaneously-live slots must hold pairwise
    // disjoint byte intervals.  The loop runs to 7 because that is the largest
    // death_op among the slots.  The helper excludes external slots, so only the
    // internal live set is checked.
    using crucible::live_intervals_disjoint_at;
    for (uint32_t t = 0; t <= 7; ++t) {
        bool disjoint = live_intervals_disjoint_at<4>(slots.view(), OpIndex{t});
        assert(disjoint);
    }

    crucible::test::pass("test_memory_plan: all tests passed\n");
    ::fixy::report(::fixy::Sink::Out, "  pool_bytes: {}\n", static_cast<unsigned long>(plan->pool_bytes));
    ::fixy::report(::fixy::Sink::Out, "  slot offsets: [{}, {}, {}, {}]\n",
                   static_cast<unsigned long>(slots.at(::foundation::core::index<0>()).offset_bytes),
                   static_cast<unsigned long>(slots.at(::foundation::core::index<1>()).offset_bytes),
                   static_cast<unsigned long>(slots.at(::foundation::core::index<2>()).offset_bytes),
                   static_cast<unsigned long>(slots.at(::foundation::core::index<3>()).offset_bytes));
    return 0;
}
