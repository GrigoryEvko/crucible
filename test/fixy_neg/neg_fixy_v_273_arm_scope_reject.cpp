// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-273 mint-gate fixture (class: CROSS-TRUNK SCOPE — clause b).
//
// Violation: the slot handle declares an ARM-shareability scope
// (MemoryScope::Inner = DMB ISH), but an async-copy / mbarrier pipeline is
// a GPU primitive whose slots must publish at an accel-trunk scope
// (Warp..Gpu).  CtxFitsAsyncPipeline's `mem_scope_is_accel(Handle::scope)`
// clause is false, so mint_async_pipeline_consumer_session is not selected.
//
// Distinct from neg_expect_tx_mismatch (that fails clause a — byte width;
// this fails clause b — scope trunk), proving the gate's two clauses are
// independent and BOTH new mints are protected (this exercises the
// CONSUMER mint, the byte fixture exercises the PRODUCER mint).
//
// Expected diagnostic: constraints not satisfied / no matching function /
//                      CtxFitsAsyncPipeline.

#include <crucible/sessions/AsyncPipelineSession.h>

#include <utility>

namespace aps = crucible::safety::proto::async_pipeline_session;
namespace eff = crucible::effects;

namespace {
struct SlotTag {};
struct ArmHandle {
    using slot_tag = SlotTag;
    static constexpr std::size_t slot_bytes = 256;
    static constexpr std::size_t stages     = 2;
    // ARM-trunk scope: an mbarrier pipeline cannot publish here.
    static constexpr crucible::algebra::lattices::MemoryScope scope =
        crucible::algebra::lattices::MemoryScope::Inner;
    void arrive_expect_tx(std::size_t) noexcept {}
    [[nodiscard]] bool try_wait(std::uint32_t) noexcept { return true; }
};
}  // namespace

int main() {
    eff::HotFgCtx ctx;
    ArmHandle handle{};
    // Bytes match (256), but scope is ARM-trunk — gate clause (b) fails.
    auto bad = aps::mint_async_pipeline_consumer_session<256>(ctx, handle);
    (void)bad;
    return 0;
}
