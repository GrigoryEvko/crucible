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
    const pid_t pid = ::fork();  // SPAWN-PROCESS-OK: death test, the refused slot ends the process
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
    if (::waitpid(pid, &status, 0) != pid) std::abort();  // SPAWN-PROCESS-OK: reaps the child forked above
    result.was_aborted = WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT;
    return result;
}

// One internal slot with the given lifetime, and one internal slot with a
// lifetime that the planner accepts.
void plan_two_slots(std::uint32_t birth_op, std::uint32_t death_op) {
    auto test = ::foundation::effects::testing::test();
    crucible::BackgroundThread bt;
    crucible::TensorSlot slots[2]{};
    slots[0].nbytes = 1024;
    slots[0].birth_op = crucible::OpIndex{0};
    slots[0].death_op = crucible::OpIndex{2};
    slots[0].slot_id = crucible::SlotId{0};
    slots[1].nbytes = 1024;
    slots[1].birth_op = crucible::OpIndex{birth_op};
    slots[1].death_op = crucible::OpIndex{death_op};
    slots[1].slot_id = crucible::SlotId{1};
    auto* plan = bt.compute_memory_plan(test.alloc, slots, 2);
    std::printf("  the planner returned a plan of %llu bytes\n", static_cast<unsigned long long>(plan->pool_bytes));
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
        {"a birth_op after the death_op", &plan_birth_after_death, "birth_op.raw() <= slots[s].death_op.raw()"},
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
    std::printf("  test_refuses_slot_lifetimes_outside_the_sweep: PASSED\n");
}

}  // namespace

int main() {
    test_refuses_slot_lifetimes_outside_the_sweep();
    auto test = ::foundation::effects::testing::test();
    crucible::BackgroundThread bt;

    // The intervals are chosen so slot 2 can reuse slot 1's space: slot 1 dies at
    // op 3 and slot 2 is born at op 4.  Slot 3 is external and takes no pool
    // space.
    constexpr uint32_t N = 4;
    crucible::TensorSlot slots[N]{};

    using crucible::SlotId;
    using crucible::OpIndex;
    using crucible::ScalarType;
    using crucible::DeviceType;
    using crucible::Layout;
    slots[0] = {.offset_bytes = 0,
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
    slots[1] = {.offset_bytes = 0,
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
    slots[2] = {.offset_bytes = 0,
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
    slots[3] = {.offset_bytes = 0,
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

    auto* plan = bt.compute_memory_plan(test.alloc, slots, N);
    assert(plan != nullptr);
    assert(plan->num_slots == N);
    assert(plan->num_external == 1);
    assert(plan->pool_bytes > 0);

    assert(slots[2].offset_bytes <= slots[1].offset_bytes + 2048);

    uint64_t total_internal = 1024 + 2048 + 1024;
    assert(plan->pool_bytes < total_internal);

    // At every op boundary the simultaneously-live slots must hold pairwise
    // disjoint byte intervals.  The loop runs to 7 because that is the largest
    // death_op among the slots.  The helper excludes external slots, so only the
    // internal live set is checked.
    using crucible::live_intervals_disjoint_at;
    std::span<const crucible::TensorSlot> slot_span{slots, N};
    for (uint32_t t = 0; t <= 7; ++t) {
        bool disjoint = live_intervals_disjoint_at<4>(slot_span, OpIndex{t});
        assert(disjoint);
    }

    std::printf("test_memory_plan: all tests passed\n");
    std::printf("  pool_bytes: %lu\n", static_cast<unsigned long>(plan->pool_bytes));
    std::printf("  slot offsets: [%lu, %lu, %lu, %lu]\n", static_cast<unsigned long>(slots[0].offset_bytes),
                static_cast<unsigned long>(slots[1].offset_bytes), static_cast<unsigned long>(slots[2].offset_bytes),
                static_cast<unsigned long>(slots[3].offset_bytes));
    return 0;
}
