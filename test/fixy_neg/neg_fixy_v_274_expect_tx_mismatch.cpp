// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-274 mint_async_pipeline guarantee fixture (class: EXPECT-TX
// MISMATCH — the §XXI gate clause).
//
// Violation: the caller asks for an mbarrier expect_tx of 128 bytes
// (`mint_async_pipeline<128>`) but the slot handle is armed for 256.  The
// V-273 CtxFitsAsyncPipeline clause folded into CtxFitsAsyncPipelineMint
// requires `Bytes == Handle::slot_bytes`; 128 != 256 unsatisfies the
// mint's requires-clause, so overload resolution finds no viable
// mint_async_pipeline.  An mbarrier armed for N bytes that validates a
// copy of M != N would hang the consumer's try_wait forever — caught at
// compile time instead.
//
// Distinct mismatch class from neg_nonctx (ctx not an ExecCtx) and
// neg_consteval_slot_mismatch (compile-time Refined VC poison).
//
// Expected diagnostic: constraints not satisfied / no matching function /
// CtxFitsAsyncPipeline.

#include <crucible/fixy/AsyncPipeline.h>

#include <array>
#include <utility>

namespace ap  = crucible::fixy::async_pipeline;
namespace saf = crucible::safety;
namespace eff = crucible::effects;
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
    eff::HotFgCtx ctx;
    Handle handle{};
    std::array<crucible::TensorSlot, 1> slots{};
    slots[0] = crucible::TensorSlot{.nbytes = 256u, .slot_id = crucible::SlotId{0u}};
    crucible::MemoryPlan plan{};
    plan.slots = slots.data();
    plan.num_slots = 1u;

    // Bytes=128 disagrees with the handle's slot_bytes=256 → gate rejects.
    auto pair = ap::mint_async_pipeline<128>(
        ctx, &plan, crucible::SlotId{0u}, handle,
        saf::mint_permission_root<SlotTag>());
    (void)pair;
    return 0;
}
