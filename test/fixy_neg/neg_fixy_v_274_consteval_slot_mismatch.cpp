// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-274 mint_async_pipeline guarantee fixture (class: COMPILE-TIME
// REFINED VC POISON — the "Bytes = Refined<equals_slot_size> from the
// MemoryPlan" premise enforced at consteval).
//
// Violation: derive_expect_tx<128> is asked to bind expect_tx=128 against
// a CONSTEXPR MemoryPlan whose slot is 256 bytes.  This is the compile-time
// arm of mint_async_pipeline's expect_tx binding: the CRUCIBLE_PRE guard
// in derive_expect_tx fires `__builtin_trap()` inside `if consteval`,
// poisoning the surrounding consteval call into "non-constant condition"
// (Refined's own vanilla pre() can be consteval-bypassed on GCC 16.1.1, so
// the helper guards explicitly).  A producer arrive.expect_tx that
// disagrees with the planned slot is rejected before any kernel runs.
//
// Distinct mismatch class from neg_expect_tx_mismatch (the §XXI gate
// clause, runtime-shaped handle) and neg_nonctx (ctx not an ExecCtx): this
// one exercises the consteval Refined VC discharge on a static-shape plan.
//
// Expected diagnostic: non-constant condition / called in a constant
// expression / __builtin_trap.

#include <crucible/fixy/AsyncPipeline.h>

#include <array>
#include <cstdint>

namespace ap = crucible::fixy::async_pipeline;

namespace {
// A constexpr single-slot plan: slot 0 is 256 bytes.
consteval std::uint64_t bound_expect_tx_against_256_plan() {
    std::array<crucible::TensorSlot, 1> slots{};
    slots[0] = crucible::TensorSlot{.nbytes = 256u, .slot_id = crucible::SlotId{0u}};
    crucible::MemoryPlan plan{};
    plan.slots = slots.data();
    plan.num_slots = 1u;
    // Bytes=128 disagrees with the planned 256B slot → CRUCIBLE_PRE poison.
    return ap::derive_expect_tx<128>(plan, crucible::SlotId{0u}).into();
}
}  // namespace

// Forcing the consteval evaluation surfaces the poison as a hard error.
static_assert(bound_expect_tx_against_256_plan() == 128u,
              "unreachable — derive_expect_tx<128> poisons the consteval call");

int main() { return 0; }
