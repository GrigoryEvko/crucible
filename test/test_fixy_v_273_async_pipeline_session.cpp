// A header-only surface is only checked where a translation unit pulls it
// in. Compiling this file runs the included header's own static_asserts
// under the project warning flags, then drives one round trip per side.

#include <crucible/sessions/AsyncPipelineSession.h>

#include <cstdint>
#include <cstdio>
#include <type_traits>
#include <utility>

namespace aps = crucible::safety::proto::async_pipeline_session;
namespace prot = crucible::safety::proto;
namespace saf = crucible::safety;
namespace eff = crucible::effects;
using MS = crucible::algebra::lattices::MemoryScope;

namespace {

struct SlotTag {};

// A non-blocking mbarrier oracle. try_wait always succeeds, so one thread
// can sequence the producer half-protocol then the consumer one. The real
// cross-thread phase dependency deadlocks when run that way.
struct FakeSlotHandle {
    using slot_tag = SlotTag;
    static constexpr std::size_t slot_bytes = 256;
    static constexpr std::size_t stages = 3;
    static constexpr MS scope = MS::Cta;

    std::uint32_t arrivals = 0;
    std::uint32_t waits = 0;
    constexpr void arrive_expect_tx(std::size_t) noexcept { ++arrivals; }
    [[nodiscard]] constexpr bool try_wait(std::uint32_t) noexcept {
        ++waits;
        return true;
    }
};

static_assert(aps::AsyncPipelineSlotHandle<FakeSlotHandle>);
static_assert(aps::CtxFitsAsyncPipeline<256, FakeSlotHandle>);
static_assert(!aps::CtxFitsAsyncPipeline<128, FakeSlotHandle>, "expect_tx mismatch must fail the gate");

using Prod = aps::ProducerProto<SlotTag, 256>;
using Cons = aps::ConsumerProto<SlotTag, 256>;
static_assert(std::is_same_v<prot::dual_of_t<Prod>, Cons>);

using ProdSession = aps::ProducerSessionHandle<256, FakeSlotHandle>;
using ConsSession = aps::ConsumerSessionHandle<256, FakeSlotHandle>;
static_assert(std::is_same_v<typename ProdSession::perm_set, prot::PermSet<SlotTag>>);
static_assert(std::is_same_v<typename ConsSession::perm_set, prot::EmptyPermSet>);

}  // namespace

int main() {
    eff::HotFgCtx ctx;

    FakeSlotHandle prod_handle{};
    auto p0 = aps::mint_async_pipeline_producer_session<256>(ctx, prod_handle, saf::mint_permission_root<SlotTag>());

    auto p1 = std::move(p0).send(
        aps::Transferable<aps::SmemFill<256>, SlotTag>{aps::SmemFill<256>{}, saf::mint_permission_root<SlotTag>()},
        aps::fill_send_transport);
    static_assert(std::is_same_v<typename decltype(p1)::perm_set, prot::EmptyPermSet>,
                  "after Send<Transferable> the producer must not hold the slot");

    auto [drain, p2] = std::move(p1).recv(aps::recv_drain_transport<256, SlotTag>());
    (void)drain;
    static_assert(std::is_same_v<typename decltype(p2)::perm_set, prot::PermSet<SlotTag>>,
                  "after Recv<Returned> the producer must regain the slot");
    std::move(p2).detach(prot::detach_reason::TestInstrumentation{});

    if (prod_handle.arrivals != 1) {
        std::fprintf(stderr, "sentinel fail: producer arrive count %u != 1\n", prod_handle.arrivals);
        return 1;
    }

    FakeSlotHandle cons_handle{};
    auto c0 = aps::mint_async_pipeline_consumer_session<256>(ctx, cons_handle);

    auto [fill, c1] = std::move(c0).recv(aps::recv_fill_transport<256, SlotTag>());
    static_assert(std::is_same_v<typename decltype(c1)::perm_set, prot::PermSet<SlotTag>>,
                  "after Recv<Transferable> the consumer must hold the slot");

    auto c2 =
        std::move(c1).send(aps::Returned<aps::SmemDrain<256>, SlotTag>{aps::SmemDrain<256>{}, std::move(fill.perm)},
                           aps::drain_send_transport);
    static_assert(std::is_same_v<typename decltype(c2)::perm_set, prot::EmptyPermSet>,
                  "after Send<Returned> the consumer must release the slot");
    std::move(c2).detach(prot::detach_reason::TestInstrumentation{});

    aps::SmemSlot<256, 3, MS::Cta> fence{aps::SmemSlotData<256, 3>{.offset_bytes = 64}};
    aps::LinearSmemSlot<256, 3, MS::Cta> slot{std::in_place, std::move(fence)};
    auto fenced = std::move(slot).consume();
    if (fenced.peek().offset_bytes != 64) {
        std::fprintf(stderr, "sentinel fail: slot offset round-trip\n");
        return 1;
    }

    std::printf("sentinel ok (prod arrivals=%u, cons waits=%u)\n", prod_handle.arrivals, cons_handle.waits);
    return 0;
}
