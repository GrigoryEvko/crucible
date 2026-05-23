// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-273 AsyncPipeline guarantee fixture (class: NO-LEAKED-SLOT).
//
// Violation: a protocol that receives a slot (Recv<Returned<…,SlotTag>>
// inserts SlotTag into the PS) and then reaches End WITHOUT returning it —
// Recv<Returned<SmemDrain<256>, SlotTag>, End>.  close()/End requires
// EmptyPermSet, so permission_flow_closes fails: a half-finished round trip
// that took a slot but never returned it cannot reach a clean close.
//
// Distinct from the producer double-fill (Loop balance) and the typestate
// fixtures — this is the End-with-non-empty-PS leak guard.
//
// Expected diagnostic: permission_flow_closes / constraints not satisfied /
//                      static assertion failed.

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

// Recv inserts SlotTag, then End with PS = {SlotTag} != EmptyPermSet.
using LeakyProto =
    prot::Recv<aps::Returned<aps::SmemDrain<256>, SlotTag>, prot::End>;
}  // namespace

int main() {
    eff::HotFgCtx ctx;
    Handle handle{};
    auto bad = prot::mint_permissioned_session<LeakyProto>(ctx, &handle);
    (void)bad;
    return 0;
}
