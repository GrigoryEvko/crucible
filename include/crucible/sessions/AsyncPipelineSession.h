#pragma once

// Each protocol step is one mbarrier operation on the slot. The producer sends
// SmemFill once its copy lands: that step is the arrive-with-expect-tx, and it
// hands the filled slot to the consumer. The consumer receives it when its
// barrier wait succeeds, and then owns the slot. The consumer sends SmemDrain
// once the slot is read out, and the producer receives that before the refill.
//
// The phase bit of the barrier is never modeled as a number. The permission
// balance of the loop is the phase invariant. A producer that refills before the
// consumer drains reaches the loop head without the slot permission, and the
// balance check rejects that at compile time.
//
// The slot value is Linear, so a second fill of a slot already in flight is a
// separate compile error at the value level. One guard covers the wire token and
// the other covers the value. A round trip that takes the slot and never returns
// it cannot close, because close demands an empty permission set.
//
// The shape is binary. A multi-warp arrival count is multiparty and does not fit
// a two-role protocol.
//
// The session stores the address of the slot handle rather than the handle. The
// handle outlives the session it is bound to.

#include <crucible/Platform.h>
#include <crucible/algebra/lattices/_MemoryScopeLattice.h>
#include <crucible/permissions/Permission.h>
#include <crucible/safety/Linear.h>
#include <crucible/safety/ScopedFence.h>
#include <crucible/sessions/PermissionedSession.h>
#include <crucible/sessions/Session.h>
#include <crucible/sessions/SessionGrade.h>
#include <crucible/sessions/SessionMint.h>
#include <crucible/sessions/SessionPermPayloads.h>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace crucible::safety::proto::async_pipeline_session {

using MemoryScope = ::crucible::algebra::lattices::MemoryScope;
using ::crucible::algebra::lattices::mem_scope_is_accel;

using ::crucible::safety::proto::Transferable;
using ::crucible::safety::proto::Returned;
using ::crucible::safety::proto::Borrowed;

// expect_tx_bytes is the transaction width the barrier is armed for, so both
// ends must agree on it.

template <std::size_t Bytes>
struct SmemFill {
    static constexpr std::size_t expect_tx_bytes = Bytes;
    std::uint32_t phase = 0;
    [[nodiscard]] constexpr bool operator==(const SmemFill&) const noexcept = default;
};

template <std::size_t Bytes>
struct SmemDrain {
    static constexpr std::size_t expect_tx_bytes = Bytes;
    std::uint32_t phase = 0;
    [[nodiscard]] constexpr bool operator==(const SmemDrain&) const noexcept = default;
};

template <std::size_t Bytes, std::size_t Stages>
struct SmemSlotData {
    static_assert(Bytes % 16 == 0, "SmemSlotData<Bytes>: TMA expect_tx must be 16-byte aligned");
    static_assert(Stages >= 1, "SmemSlotData<Stages>: pipeline depth must be >= 1");

    static constexpr std::size_t bytes = Bytes;
    static constexpr std::size_t stages = Stages;

    std::uint32_t offset_bytes = 0;
    std::uint32_t phase = 0;

    [[nodiscard]] constexpr bool operator==(const SmemSlotData&) const noexcept = default;
};

template <std::size_t Bytes, std::size_t Stages, MemoryScope Scope>
using SmemSlot = ::crucible::safety::ScopedFence<Scope, SmemSlotData<Bytes, Stages>>;

template <std::size_t Bytes, std::size_t Stages, MemoryScope Scope>
using LinearSmemSlot = ::crucible::safety::Linear<SmemSlot<Bytes, Stages, Scope>>;

// Neither protocol has an exit branch, so neither ever reaches End. The caller
// detaches the terminal handle at shutdown.

template <typename SlotTag, std::size_t Bytes>
using ProducerProto =
    Loop<Send<Transferable<SmemFill<Bytes>, SlotTag>, Recv<Returned<SmemDrain<Bytes>, SlotTag>, Continue>>>;

template <typename SlotTag, std::size_t Bytes>
using ConsumerProto = dual_of_t<ProducerProto<SlotTag, Bytes>>;

template <typename Handle>
concept AsyncPipelineSlotHandle = requires {
    typename Handle::slot_tag;
    requires std::convertible_to<decltype(Handle::slot_bytes), std::size_t>;
    requires std::convertible_to<decltype(Handle::stages), std::size_t>;
    requires std::same_as<std::remove_cv_t<decltype(Handle::scope)>, MemoryScope>;
    requires(Handle::stages >= 1);
} && requires(Handle& handle, std::size_t bytes, std::uint32_t phase) {
    { handle.arrive_expect_tx(bytes) } -> std::same_as<void>;
    { handle.try_wait(phase) } -> std::same_as<bool>;
};

// A barrier armed for one transaction width cannot validate a copy of another,
// so the declared byte count must equal the capacity of the handle.

template <std::size_t Bytes, typename Handle>
concept CtxFitsAsyncPipeline =
    AsyncPipelineSlotHandle<Handle> && (Bytes == static_cast<std::size_t>(Handle::slot_bytes))
    && mem_scope_is_accel(Handle::scope);

template <std::size_t Bytes, typename Handle, ::crucible::effects::IsExecCtx Ctx>
    requires CtxFitsAsyncPipeline<Bytes, Handle>
[[nodiscard]] constexpr auto
mint_async_pipeline_producer_session(Ctx const& ctx, Handle& handle,
                                     ::crucible::safety::Permission<typename Handle::slot_tag>&& slot_perm) noexcept {
    using SlotTag = typename Handle::slot_tag;
    return mint_permissioned_session<ProducerProto<SlotTag, Bytes>>(ctx, &handle, std::move(slot_perm));
}

template <std::size_t Bytes, typename Handle, ::crucible::effects::IsExecCtx Ctx>
    requires CtxFitsAsyncPipeline<Bytes, Handle>
[[nodiscard]] constexpr auto mint_async_pipeline_consumer_session(Ctx const& ctx, Handle& handle) noexcept {
    using SlotTag = typename Handle::slot_tag;
    return mint_permissioned_session<ConsumerProto<SlotTag, Bytes>>(ctx, &handle);
}

template <std::size_t Bytes, typename Handle, ::crucible::effects::IsExecCtx Ctx = ::crucible::effects::HotFgCtx>
using ProducerSessionHandle = decltype(mint_async_pipeline_producer_session<Bytes>(
    std::declval<Ctx const&>(), std::declval<Handle&>(),
    std::declval<::crucible::safety::Permission<typename Handle::slot_tag>&&>()));

template <std::size_t Bytes, typename Handle, ::crucible::effects::IsExecCtx Ctx = ::crucible::effects::HotFgCtx>
using ConsumerSessionHandle =
    decltype(mint_async_pipeline_consumer_session<Bytes>(std::declval<Ctx const&>(), std::declval<Handle&>()));

// A send helper does not consume the permission itself. The payload carries it,
// and it is consumed when the payload destructs at the end of the statement.
//
// A recv helper must return the exact payload type the protocol names, carrying
// a permission the recipient gains. A generic lambda cannot name that type, so
// the two recv helpers are function templates that return the lambda.
//
// The recv helpers mint a root permission. That stands in for the barrier phase,
// which is the real source of the slot permission on hardware.

inline constexpr auto fill_send_transport = [](auto& hp, auto&& fill) noexcept {
    using FillT = std::remove_cvref_t<decltype(fill.value)>;
    hp->arrive_expect_tx(FillT::expect_tx_bytes);
};

inline constexpr auto drain_send_transport = [](auto& hp, auto&& drain) noexcept {
    using DrainT = std::remove_cvref_t<decltype(drain.value)>;
    hp->arrive_expect_tx(DrainT::expect_tx_bytes);
};

template <std::size_t Bytes, typename SlotTag>
[[nodiscard]] constexpr auto recv_fill_transport() noexcept {
    return [](auto& hp) noexcept {
        while (!hp->try_wait(0u)) {
            CRUCIBLE_SPIN_PAUSE;
        }
        return Transferable<SmemFill<Bytes>, SlotTag>{SmemFill<Bytes>{},
                                                      ::crucible::safety::mint_permission_root<SlotTag>()};
    };
}

template <std::size_t Bytes, typename SlotTag>
[[nodiscard]] constexpr auto recv_drain_transport() noexcept {
    return [](auto& hp) noexcept {
        while (!hp->try_wait(0u)) {
            CRUCIBLE_SPIN_PAUSE;
        }
        return Returned<SmemDrain<Bytes>, SlotTag>{SmemDrain<Bytes>{},
                                                   ::crucible::safety::mint_permission_root<SlotTag>()};
    };
}

}  // namespace crucible::safety::proto::async_pipeline_session

namespace crucible::safety::proto::async_pipeline_session::detail::self_test {

struct SlotTag {};

struct FakeSlotHandle {
    using slot_tag = SlotTag;
    static constexpr std::size_t slot_bytes = 256;
    static constexpr std::size_t stages = 2;
    static constexpr MemoryScope scope = MemoryScope::Cta;

    std::uint32_t arrivals = 0;
    constexpr void arrive_expect_tx(std::size_t) noexcept { ++arrivals; }
    [[nodiscard]] constexpr bool try_wait(std::uint32_t) noexcept { return true; }
};

static_assert(AsyncPipelineSlotHandle<FakeSlotHandle>);
static_assert(CtxFitsAsyncPipeline<256, FakeSlotHandle>);

using Prod = ProducerProto<SlotTag, 256>;
using Cons = ConsumerProto<SlotTag, 256>;

static_assert(
    std::is_same_v<
        Prod, Loop<Send<Transferable<SmemFill<256>, SlotTag>, Recv<Returned<SmemDrain<256>, SlotTag>, Continue>>>>);
static_assert(
    std::is_same_v<
        Cons, Loop<Recv<Transferable<SmemFill<256>, SlotTag>, Send<Returned<SmemDrain<256>, SlotTag>, Continue>>>>);
static_assert(std::is_same_v<dual_of_t<Prod>, Cons>, "AsyncPipeline producer/consumer must be exact duals "
                                                     "(deadlock-freedom witness)");
static_assert(std::is_same_v<dual_of_t<Cons>, Prod>);

using Slot = LinearSmemSlot<256, 2, MemoryScope::Cta>;
static_assert(
    std::is_same_v<
        Slot, ::crucible::safety::Linear<::crucible::safety::ScopedFence<MemoryScope::Cta, SmemSlotData<256, 2>>>>);
static_assert(SmemSlotData<256, 2>::bytes == 256);
static_assert(SmemSlotData<256, 2>::stages == 2);

using ProdSession = ProducerSessionHandle<256, FakeSlotHandle>;
using ConsSession = ConsumerSessionHandle<256, FakeSlotHandle>;

static_assert(std::is_same_v<typename ProdSession::perm_set, PermSet<SlotTag>>,
              "producer session must start holding the slot permission");
static_assert(std::is_same_v<typename ConsSession::perm_set, EmptyPermSet>,
              "consumer session must start without the slot permission");

static_assert(perm_set_equal_v<compute_perm_set_after_send_t<PermSet<SlotTag>, Transferable<SmemFill<256>, SlotTag>>,
                               EmptyPermSet>,
              "Send<Transferable<…,SlotTag>> must drop SlotTag from the producer PS");
static_assert(
    perm_set_equal_v<compute_perm_set_after_recv_t<EmptyPermSet, Returned<SmemDrain<256>, SlotTag>>, PermSet<SlotTag>>,
    "Recv<Returned<…,SlotTag>> must restore SlotTag to the producer PS");

}  // namespace crucible::safety::proto::async_pipeline_session::detail::self_test
