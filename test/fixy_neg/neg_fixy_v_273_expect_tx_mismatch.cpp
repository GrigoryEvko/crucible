// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-273 mint-gate fixture (class: EXPECT_TX MISMATCH — clause a).
//
// Violation: the producer mint declares Bytes = 128 but the slot handle's
// slot_bytes capacity is 256.  An mbarrier armed for one transaction width
// cannot validate a TMA copy of a different width, so
// CtxFitsAsyncPipeline<128, Handle> fails its `Bytes == Handle::slot_bytes`
// clause and mint_async_pipeline_producer_session is not selected.  This is
// the FIXY-V-274 hook: the plan-derived Refined<equals_slot_size> Bytes
// must agree with the handle.
//
// Expected diagnostic: constraints not satisfied / no matching function /
//                      CtxFitsAsyncPipeline.

#include <crucible/sessions/AsyncPipelineSession.h>

#include <utility>

namespace aps = crucible::safety::proto::async_pipeline_session;
namespace eff = crucible::effects;

namespace {
struct SlotTag {};
struct Handle {
    using slot_tag = SlotTag;
    static constexpr std::size_t slot_bytes = 256;
    static constexpr std::size_t stages     = 2;
    static constexpr crucible::algebra::lattices::MemoryScope scope =
        crucible::algebra::lattices::MemoryScope::Cta;
    void arrive_expect_tx(std::size_t) noexcept {}
    [[nodiscard]] bool try_wait(std::uint32_t) noexcept { return true; }
};
}  // namespace

int main() {
    eff::HotFgCtx ctx;
    Handle handle{};
    // Bytes=128 disagrees with Handle::slot_bytes=256 — gate clause (a) fails.
    auto bad = aps::mint_async_pipeline_producer_session<128>(
        ctx, handle, crucible::safety::mint_permission_root<SlotTag>());
    (void)bad;
    return 0;
}
