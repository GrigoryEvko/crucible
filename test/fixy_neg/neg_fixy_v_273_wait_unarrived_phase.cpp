// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-273 AsyncPipeline guarantee fixture (class: TYPESTATE / PHASE).
//
// Violation: the consumer tries to Send the drain-ack (signal "slot
// refillable") BEFORE Recv-ing the fill — i.e. it acts on a phase the
// producer has not reached.  The consumer session's head is
// Recv<Transferable<SmemFill,SlotTag>, …>; that PermissionedSessionHandle
// specialization exposes .recv() and has NO .send() member, so calling
// .send() first is a COMPILE ERROR.  A consumer cannot wait-then-signal
// on an un-arrived phase.
//
// Distinct from neg_double_fill (that is a producer PS-balance failure at
// mint; this is a consumer typestate failure at the call site).
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
    auto consumer = aps::mint_async_pipeline_consumer_session<256>(ctx, handle);

    // Consumer head is Recv<Transferable<…>> — sending the drain-ack now,
    // before the fill arrives, is a typestate error (no .send() member).
    auto bad = std::move(consumer).send(
        aps::Returned<aps::SmemDrain<256>, SlotTag>{
            aps::SmemDrain<256>{}, crucible::safety::mint_permission_root<SlotTag>()},
        aps::drain_send_transport);
    (void)bad;
    return 0;
}
