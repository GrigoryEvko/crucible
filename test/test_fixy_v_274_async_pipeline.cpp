// FIXY-V-274 sentinel TU — verifies fixy/AsyncPipeline.h under the
// project's warning / contract flags (header-only static_asserts are only
// checked when a real .cpp TU includes the header — see
// feedback_header_only_static_assert_blind_spot).  Re-asserts the
// derivation helpers (slot lookup, live-set-width → stages, expect_tx
// binding) and then drives a full producer + consumer round trip against
// a session pair MINTED through mint_async_pipeline from a runtime
// MemoryPlan.

#include <crucible/fixy/AsyncPipeline.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <type_traits>
#include <utility>

namespace ap   = crucible::fixy::async_pipeline;
namespace aps  = crucible::safety::proto::async_pipeline_session;
namespace prot = crucible::safety::proto;
namespace saf  = crucible::safety;
namespace eff  = crucible::effects;
using MS = crucible::algebra::lattices::MemoryScope;

namespace {

struct SlotTag {};

// Non-blocking mbarrier oracle stub (same shape as the V-273 sentinel):
// try_wait always succeeds so one thread can sequence producer-then-
// consumer half-protocols without the real cross-thread phase dependency.
// stages = 2 must equal the plan-derived pipeline depth (live width 2).
struct FakeSlotHandle {
    using slot_tag = SlotTag;
    static constexpr std::size_t slot_bytes = 256;
    static constexpr std::size_t stages     = 2;
    static constexpr MS          scope      = MS::Cta;

    std::uint32_t arrivals = 0;
    std::uint32_t waits    = 0;
    void arrive_expect_tx(std::size_t) noexcept { ++arrivals; }
    [[nodiscard]] bool try_wait(std::uint32_t) noexcept {
        ++waits;
        return true;
    }
};

// ── Compile-time witnesses (mirror the in-header self-test) ─────────
static_assert(ap::equals_slot_size<256>(std::uint64_t{256}));
static_assert(!ap::equals_slot_size<256>(std::uint64_t{128}));
static_assert(sizeof(ap::ExpectTxBytes<256>) == sizeof(std::uint64_t));
static_assert(ap::CtxFitsAsyncPipelineMint<256, FakeSlotHandle, eff::HotFgCtx>);
// A handle whose pinned depth disagrees with the slot byte count is still
// gated by the V-273 CtxFitsAsyncPipeline clause (expect_tx mismatch).
static_assert(!aps::CtxFitsAsyncPipeline<128, FakeSlotHandle>);

// Build a runtime two-slot plan: slot 0 lives [0,3] sized 256B; slot 1
// lives [2,5] sized 256B → max live width 2 over slot 0's window.
[[nodiscard]] std::array<crucible::TensorSlot, 2> make_slots() noexcept {
    std::array<crucible::TensorSlot, 2> slots{};
    slots[0] = crucible::TensorSlot{.offset_bytes = 0u, .nbytes = 256u,
        .birth_op = crucible::OpIndex{0u}, .death_op = crucible::OpIndex{3u},
        .slot_id = crucible::SlotId{0u}};
    slots[1] = crucible::TensorSlot{.offset_bytes = 256u, .nbytes = 256u,
        .birth_op = crucible::OpIndex{2u}, .death_op = crucible::OpIndex{5u},
        .slot_id = crucible::SlotId{1u}};
    return slots;
}

}  // namespace

int main() {
    eff::HotFgCtx ctx;

    std::array<crucible::TensorSlot, 2> slots = make_slots();
    crucible::MemoryPlan plan{};
    plan.slots = slots.data();
    plan.num_slots = 2u;

    // ── Derivation helpers agree with the plan ──────────────────────
    if (ap::slot_nbytes(plan, crucible::SlotId{0u}) != 256u) {
        std::fprintf(stderr, "V274 SENTINEL FAIL: slot_nbytes != 256\n");
        return 1;
    }
    if (ap::derive_pipeline_stages(plan, crucible::SlotId{0u}, 233472u) != 2u) {
        std::fprintf(stderr, "V274 SENTINEL FAIL: derived stages != 2\n");
        return 1;
    }

    // ── Mint the session pair from the runtime plan ─────────────────
    FakeSlotHandle handle{};
    auto pair = ap::mint_async_pipeline<256>(
        ctx, &plan, crucible::SlotId{0u}, handle,
        saf::mint_permission_root<SlotTag>());

    static_assert(std::is_same_v<typename decltype(pair.producer)::perm_set,
                                 prot::PermSet<SlotTag>>,
                  "minted producer must start holding the slot permission");
    static_assert(std::is_same_v<typename decltype(pair.consumer)::perm_set,
                                 prot::EmptyPermSet>,
                  "minted consumer must start without the slot permission");

    // ── Producer round trip: fill (hand off slot) → drain recv (regain) ─
    auto p1 = std::move(pair.producer).send(
        aps::Transferable<aps::SmemFill<256>, SlotTag>{
            aps::SmemFill<256>{}, saf::mint_permission_root<SlotTag>()},
        aps::fill_send_transport);
    static_assert(std::is_same_v<typename decltype(p1)::perm_set,
                                 prot::EmptyPermSet>);
    auto [drain, p2] = std::move(p1).recv(aps::recv_drain_transport<256, SlotTag>());
    (void)drain;
    static_assert(std::is_same_v<typename decltype(p2)::perm_set,
                                 prot::PermSet<SlotTag>>);
    std::move(p2).detach(prot::detach_reason::TestInstrumentation{});

    // ── Consumer round trip: fill recv (gain slot) → drain (return) ─────
    auto [fill, c1] = std::move(pair.consumer).recv(
        aps::recv_fill_transport<256, SlotTag>());
    static_assert(std::is_same_v<typename decltype(c1)::perm_set,
                                 prot::PermSet<SlotTag>>);
    auto c2 = std::move(c1).send(
        aps::Returned<aps::SmemDrain<256>, SlotTag>{
            aps::SmemDrain<256>{}, std::move(fill.perm)},
        aps::drain_send_transport);
    static_assert(std::is_same_v<typename decltype(c2)::perm_set,
                                 prot::EmptyPermSet>);
    std::move(c2).detach(prot::detach_reason::TestInstrumentation{});

    if (handle.arrivals != 2u) {
        std::fprintf(stderr, "V274 SENTINEL FAIL: arrive count %u != 2\n",
                     handle.arrivals);
        return 1;
    }

    std::printf("V274 SENTINEL OK (arrivals=%u, waits=%u)\n",
                handle.arrivals, handle.waits);
    return 0;
}
