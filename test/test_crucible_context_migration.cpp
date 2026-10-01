// A switch of region copies the slots of the shared prefix into the new pool,
// and a bitset of MIGRATION_MAX_SLOTS bits records each slot that it copied.
// A plan with more slots than the bitset is refused before a slot is copied,
// under each contract semantic.  The build compiles this file twice: one time
// under the contract semantic of the preset, and one time under the ignore
// semantic, where no contract assertion checks and a CRUCIBLE_PRE gives its
// condition to the optimizer as an assumption.
//
// Each refusal ends the process, so each attack runs in a child process.  The
// parent reads the standard error of the child, and it requires SIGABRT and a
// report that names the bound.

#include <crucible/CrucibleContext.h>

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

using crucible::CrucibleContext;
using crucible::DeviceType;
using crucible::Layout;
using crucible::MemoryPlan;
using crucible::OpIndex;
using crucible::RegionNode;
using crucible::ScalarType;
using crucible::SchemaHash;
using crucible::ShapeHash;
using crucible::SlotId;
using crucible::TensorSlot;
using crucible::TraceEntry;
using crucible::TraceNodeKind;

// The bound of the bitset in CrucibleContext.
constexpr std::uint32_t kBitsetSlots = 1024;
// A plan two times as wide as the bitset.
constexpr std::uint32_t kWideSlots = 2 * kBitsetSlots;
constexpr std::uint64_t kSlotBytes = 256;

// Every slot is internal and kSlotBytes wide, so slot i sits at offset
// i * kSlotBytes and no two slots overlap.
MemoryPlan make_plan(TensorSlot* slots, std::uint32_t count) {
    for (std::uint32_t index = 0; index < count; index++) {
        slots[index] = {.offset_bytes = index * kSlotBytes,
                        .nbytes = kSlotBytes,
                        .birth_op = OpIndex{0},
                        .death_op = OpIndex{2},
                        .dtype = ScalarType::Float,
                        .device_type = DeviceType::CPU,
                        .device_idx = 0,
                        .layout = Layout::Strided,
                        .is_external = false,
                        .pad = {},
                        .slot_id = SlotId{index},
                        .pad2 = {}};
    }
    MemoryPlan plan{};
    plan.slots = slots;
    plan.num_slots = count;
    plan.num_external = 0;
    plan.pool_bytes = count * kSlotBytes;
    plan.device_type = DeviceType::CPU;
    plan.device_idx = 0;
    return plan;
}

// Two operations with the same hashes in each region, so the first one is
// the shared prefix of a switch at position 1.
void make_ops(TraceEntry* ops, SlotId* outputs) {
    for (std::uint32_t index = 0; index < 2; index++) {
        ops[index].schema_hash = SchemaHash{100 + index};
        ops[index].shape_hash = ShapeHash{200 + index};
        ops[index].num_outputs = 1;
        ops[index].output_slot_ids = &outputs[index];
    }
}

void init_region(RegionNode* region, TraceEntry* ops, MemoryPlan* plan) {
    ::new(region) RegionNode{};
    region->kind = TraceNodeKind::REGION;
    region->ops = ops;
    region->num_ops = 2;
    region->plan = plan;
}

// The slots of the plans live in static storage: the wide plan has 2048
// slots of 48 bytes, too many for the stack of a test.
TensorSlot old_slots[2];
TensorSlot new_slots[kWideSlots];

// Activates a region of two slots, then switches at position 1 to a region
// whose plan has new_count slots.  The first operation of the new region
// writes slot new_slot.  Returns the result of the switch.
bool switch_to_plan(std::uint32_t new_count, std::uint32_t new_slot) {
    SlotId old_outputs[2] = {SlotId{0}, SlotId{1}};
    TraceEntry old_ops[2]{};
    make_ops(old_ops, old_outputs);
    MemoryPlan old_plan = make_plan(old_slots, 2);
    RegionNode old_region{};
    init_region(&old_region, old_ops, &old_plan);

    SlotId new_outputs[2] = {SlotId{new_slot}, SlotId{0}};
    TraceEntry new_ops[2]{};
    make_ops(new_ops, new_outputs);
    MemoryPlan new_plan = make_plan(new_slots, new_count);
    RegionNode new_region{};
    init_region(&new_region, new_ops, &new_plan);

    CrucibleContext context;
    assert(context.activate(&old_region));
    const bool was_switched = context.switch_region(&new_region, 1);
    return was_switched && context.active_region() == &new_region;
}

struct ChildResult {
    bool was_aborted = false;
    char report[8192]{};
};

// Runs attack in a child process, with the standard error of the child on a
// pipe.  The child ends with exit status 0 when the attack returns.
ChildResult run_in_child(void (*attack)()) {
    ChildResult result{};
    int pipe_ends[2] = {-1, -1};
    if (::pipe(pipe_ends) != 0) std::abort();
    std::fflush(stderr);
    const pid_t pid = ::fork();  // SPAWN-PROCESS-OK: death test, the refusal ends the process
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

[[nodiscard]] bool is_refused(void (*attack)()) {
    const ChildResult result = run_in_child(attack);
    const bool names_the_bound = std::string_view{result.report}.find("MIGRATION_MAX_SLOTS") != std::string_view::npos;
    if (!result.was_aborted || !names_the_bound) {
        std::fprintf(stderr, "child: aborted=%d report:\n%s\n", result.was_aborted ? 1 : 0, result.report);
    }
    return result.was_aborted && names_the_bound;
}

// A plan at the bound of the bitset migrates.  This shows that the attacks
// below fail for the width of the plan and for no other reason.
void test_plan_at_the_bound_migrates() {
    assert(switch_to_plan(kBitsetSlots, kBitsetSlots - 1));
    std::printf("  test_plan_at_the_bound_migrates: PASSED\n");
}

// The shared prefix writes a slot that the bitset can record, and the plan is
// still too wide.  The switch is refused before it copies the slot.
void test_wide_plan_is_refused() {
    assert(is_refused([] { (void)switch_to_plan(kWideSlots, 5); }));
    std::printf("  test_wide_plan_is_refused: PASSED\n");
}

// The shared prefix writes a slot past the end of the bitset.  Without the
// refusal, the migration sets a bit outside the bitset on the stack.
void test_slot_past_the_bitset_is_refused() {
    assert(is_refused([] { (void)switch_to_plan(kWideSlots, kBitsetSlots + 476); }));
    std::printf("  test_slot_past_the_bitset_is_refused: PASSED\n");
}

}  // namespace

int main() {
    std::printf("test_crucible_context_migration:\n");
    test_plan_at_the_bound_migrates();
    test_wide_plan_is_refused();
    test_slot_past_the_bitset_is_refused();
    std::printf("test_crucible_context_migration: all tests passed\n");
    return 0;
}
