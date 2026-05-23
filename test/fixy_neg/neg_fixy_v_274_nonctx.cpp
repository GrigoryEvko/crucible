// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-274 mint_async_pipeline guarantee fixture (class: NON-CTX FIRST
// ARG — the §XXI ctx-bound discipline).
//
// Violation: a §XXI ctx-bound mint takes `Ctx const&` as its first
// parameter, constrained by `effects::IsExecCtx Ctx`.  Passing a plain
// `int` (not an ExecCtx) as the ctx argument leaves `IsExecCtx<int>`
// unsatisfied, so the mint_async_pipeline template constraint rejects the
// call.  The ctx carries the OS-effect-row authority the produced session
// inherits; an arbitrary value cannot stand in for it.
//
// Distinct mismatch class from neg_expect_tx_mismatch (Bytes != slot
// capacity) and neg_consteval_slot_mismatch (compile-time Refined poison).
//
// Expected diagnostic: constraints not satisfied / no matching function /
// IsExecCtx.

#include <crucible/fixy/AsyncPipeline.h>

#include <array>
#include <utility>

namespace ap  = crucible::fixy::async_pipeline;
namespace saf = crucible::safety;
using MS = crucible::algebra::lattices::MemoryScope;

namespace {
struct SlotTag {};
struct Handle {
    using slot_tag = SlotTag;
    static constexpr std::size_t slot_bytes = 256;
    static constexpr std::size_t stages     = 2;
    static constexpr MS          scope      = MS::Cta;
    void arrive_expect_tx(std::size_t) noexcept {}
    [[nodiscard]] bool try_wait(std::uint32_t) noexcept { return true; }
};
}  // namespace

int main() {
    int not_a_ctx = 0;
    Handle handle{};
    std::array<crucible::TensorSlot, 1> slots{};
    slots[0] = crucible::TensorSlot{.nbytes = 256u, .slot_id = crucible::SlotId{0u}};
    crucible::MemoryPlan plan{};
    plan.slots = slots.data();
    plan.num_slots = 1u;

    // First arg is a bare int, not an ExecCtx → IsExecCtx<int> unsatisfied.
    auto pair = ap::mint_async_pipeline<256>(
        not_a_ctx, &plan, crucible::SlotId{0u}, handle,
        saf::mint_permission_root<SlotTag>());
    (void)pair;
    return 0;
}
