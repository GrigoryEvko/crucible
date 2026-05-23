// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-273 AsyncPipeline guarantee fixture (class: DOUBLE-FILL / PHASE).
//
// Violation: the producer fills a slot, then tries to fill it AGAIN before
// the consumer has drained it.  After the first .send() the producer
// session's head is Recv<Returned<…,SlotTag>, Continue> — that
// PermissionedSessionHandle specialization exposes .recv() and has NO
// .send() member, so the second .send() (the premature refill) is a
// COMPILE ERROR.  Refilling a slot still in flight cannot type-check; the
// session protocol IS the phase bit.
//
// Exercises mint_async_pipeline_producer_session (HS14 producer-mint
// coverage, paired with neg_expect_tx_mismatch — two distinct mismatch
// classes: typestate misuse here, gate clause there).
//
// Expected diagnostic: no member named 'send' / has no member.

#include <crucible/sessions/AsyncPipelineSession.h>

#include <utility>

namespace aps = crucible::safety::proto::async_pipeline_session;
namespace prot = crucible::safety::proto;
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
    auto p0 = aps::mint_async_pipeline_producer_session<256>(
        ctx, handle, crucible::safety::mint_permission_root<SlotTag>());

    // First fill — hands the slot to the consumer; head becomes Recv<Returned>.
    auto p1 = std::move(p0).send(
        aps::Transferable<aps::SmemFill<256>, SlotTag>{
            aps::SmemFill<256>{}, crucible::safety::mint_permission_root<SlotTag>()},
        aps::fill_send_transport);

    // Second fill BEFORE draining — the Recv-headed handle has no .send().
    auto p2 = std::move(p1).send(
        aps::Transferable<aps::SmemFill<256>, SlotTag>{
            aps::SmemFill<256>{}, crucible::safety::mint_permission_root<SlotTag>()},
        aps::fill_send_transport);
    (void)p2;
    return 0;
}
