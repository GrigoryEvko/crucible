#pragma once

#include <crucible/MerkleDag.h>
#include <crucible/Platform.h>
#include <crucible/Types.h>
#include <crucible/algebra/lattices/MemoryScopeLattice.h>
#include <crucible/effects/ExecCtx.h>
#include <crucible/permissions/Permission.h>
#include <crucible/safety/Pre.h>
#include <crucible/safety/Refined.h>
#include <crucible/sessions/AsyncPipelineSession.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>
#include <utility>

namespace crucible::fixy::async_pipeline {

namespace aps = ::crucible::safety::proto::async_pipeline_session;
using MemoryScope = ::crucible::algebra::lattices::MemoryScope;

template <std::size_t N>
struct EqualsSlotSize {
    [[nodiscard]] constexpr bool operator()(std::uint64_t v) const noexcept { return v == N; }
};

template <std::size_t N>
inline constexpr EqualsSlotSize<N> equals_slot_size{};

template <std::size_t N>
using ExpectTxBytes = ::crucible::safety::Refined<equals_slot_size<N>, std::uint64_t>;

// The kernel configuration accepts a pipeline depth in [1, 7].
inline constexpr std::size_t kMaxPipelineStages = 7;

[[nodiscard]] constexpr ::crucible::TensorSlot const* find_slot(::crucible::MemoryPlan const& plan,
                                                                ::crucible::SlotId slot_id) noexcept {
    for (std::uint32_t i = 0; i < plan.num_slots; ++i) {
        if (plan.slots[i].slot_id == slot_id) return &plan.slots[i];
    }
    return nullptr;
}

[[nodiscard]] constexpr std::uint64_t slot_nbytes(::crucible::MemoryPlan const& plan,
                                                  ::crucible::SlotId slot_id) noexcept {
    ::crucible::TensorSlot const* found = find_slot(plan, slot_id);
    return found != nullptr ? found->nbytes : std::uint64_t{0};
}

// External slots keep their own allocations and never enter the pool, so
// they are not part of the live set.
[[nodiscard]] constexpr std::size_t live_set_width_at(std::span<const ::crucible::TensorSlot> slots,
                                                      ::crucible::OpIndex op) noexcept {
    std::size_t width = 0;
    const std::uint32_t t = op.raw();
    for (::crucible::TensorSlot const& slot : slots) {
        if (slot.is_external) continue;
        if (slot.birth_op.raw() <= t && t <= slot.death_op.raw()) ++width;
    }
    return width;
}

[[nodiscard]] constexpr std::size_t max_live_width_over_window(std::span<const ::crucible::TensorSlot> slots,
                                                               ::crucible::OpIndex birth,
                                                               ::crucible::OpIndex death) noexcept {
    if (!birth.is_valid() || !death.is_valid()) return 1;
    std::size_t widest = 0;
    for (std::uint32_t t = birth.raw(); t <= death.raw(); ++t) {
        const std::size_t current = live_set_width_at(slots, ::crucible::OpIndex{t});
        if (current > widest) widest = current;
    }
    return widest;
}

[[nodiscard]] constexpr std::size_t derive_pipeline_stages(::crucible::MemoryPlan const& plan,
                                                           ::crucible::SlotId slot_id,
                                                           std::uint32_t smem_per_sm) noexcept {
    ::crucible::TensorSlot const* found = find_slot(plan, slot_id);
    if (found == nullptr || found->nbytes == 0u) return 1;
    const std::size_t live_width =
        max_live_width_over_window({plan.slots, plan.num_slots}, found->birth_op, found->death_op);
    const std::size_t smem_cap = static_cast<std::size_t>(smem_per_sm) / found->nbytes;
    std::size_t stages = live_width;
    if (smem_cap < stages) stages = smem_cap;
    if (kMaxPipelineStages < stages) stages = kMaxPipelineStages;
    if (stages < 1) stages = 1;
    return stages;
}

// The explicit guard is load-bearing. Refined's own precondition can be
// bypassed at consteval on GCC 16.1.1, so this check is what poisons the
// surrounding consteval call. A static-shape mismatch is then a compile
// error, and a dynamic-shape mismatch aborts at construction.
template <std::size_t Bytes>
[[nodiscard]] constexpr ExpectTxBytes<Bytes> derive_expect_tx(::crucible::MemoryPlan const& plan,
                                                              ::crucible::SlotId slot_id) noexcept {
    const std::uint64_t planned = slot_nbytes(plan, slot_id);
    CRUCIBLE_PRE(equals_slot_size<Bytes>(planned));
    return ::crucible::safety::mint_refined<equals_slot_size<Bytes>, std::uint64_t>(planned);
}

template <std::size_t Bytes, typename Handle, typename Ctx>
concept CtxFitsAsyncPipelineMint = aps::CtxFitsAsyncPipeline<Bytes, Handle> && ::crucible::effects::IsExecCtx<Ctx>
                                && (static_cast<std::size_t>(Handle::stages) <= kMaxPipelineStages);

template <std::size_t Bytes, typename Handle, typename Ctx>
struct AsyncPipelinePair {
    aps::ProducerSessionHandle<Bytes, Handle, Ctx> producer;
    aps::ConsumerSessionHandle<Bytes, Handle, Ctx> consumer;
};

// The handle stays caller-owned and must outlive the returned sessions. A
// handle constructed inside this factory would dangle as soon as the
// sessions escaped.
template <std::size_t Bytes, typename Handle, ::crucible::effects::IsExecCtx Ctx>
    requires CtxFitsAsyncPipelineMint<Bytes, Handle, Ctx>
[[nodiscard]] constexpr auto mint_async_pipeline(Ctx const& ctx, ::crucible::MemoryPlan const* plan,
                                                 ::crucible::SlotId slot_id, Handle& handle,
                                                 ::crucible::safety::Permission<typename Handle::slot_tag>&& slot_perm,
                                                 std::uint32_t smem_per_sm = 233472u) noexcept
    -> AsyncPipelinePair<Bytes, Handle, Ctx> {
    CRUCIBLE_PRE(plan != nullptr);
    // A mismatch aborts at establishment, and is a compile error on a
    // constexpr plan, instead of hanging the consumer's try_wait forever.
    const ExpectTxBytes<Bytes> expect_tx = derive_expect_tx<Bytes>(*plan, slot_id);
    (void)expect_tx;
    CRUCIBLE_PRE(static_cast<std::size_t>(Handle::stages) == derive_pipeline_stages(*plan, slot_id, smem_per_sm));
    return AsyncPipelinePair<Bytes, Handle, Ctx>{
        aps::mint_async_pipeline_producer_session<Bytes>(ctx, handle, std::move(slot_perm)),
        aps::mint_async_pipeline_consumer_session<Bytes>(ctx, handle)};
}

}  // namespace crucible::fixy::async_pipeline

namespace crucible::fixy::async_pipeline::detail::self_test {

static_assert(equals_slot_size<256>(std::uint64_t{256}));
static_assert(!equals_slot_size<256>(std::uint64_t{128}));
static_assert(sizeof(ExpectTxBytes<256>) == sizeof(std::uint64_t));

struct ConstevalPlanFixture {
    std::array<::crucible::TensorSlot, 2> slots{};
    ::crucible::MemoryPlan plan{};
    constexpr ConstevalPlanFixture() noexcept {
        slots[0] = ::crucible::TensorSlot{.offset_bytes = 0u,
                                          .nbytes = 256u,
                                          .birth_op = ::crucible::OpIndex{0u},
                                          .death_op = ::crucible::OpIndex{3u},
                                          .slot_id = ::crucible::SlotId{0u}};
        slots[1] = ::crucible::TensorSlot{.offset_bytes = 256u,
                                          .nbytes = 256u,
                                          .birth_op = ::crucible::OpIndex{2u},
                                          .death_op = ::crucible::OpIndex{5u},
                                          .slot_id = ::crucible::SlotId{1u}};
        plan.slots = slots.data();
        plan.num_slots = 2u;
    }
};

consteval std::uint64_t probe_nbytes() {
    ConstevalPlanFixture fixture{};
    return slot_nbytes(fixture.plan, ::crucible::SlotId{0u});
}
static_assert(probe_nbytes() == 256u);

consteval std::size_t probe_stages(std::uint32_t smem) {
    ConstevalPlanFixture fixture{};
    return derive_pipeline_stages(fixture.plan, ::crucible::SlotId{0u}, smem);
}
static_assert(probe_stages(233472u) == 2u);
static_assert(probe_stages(256u) == 1u);
static_assert(probe_stages(512u) == 2u);

consteval std::uint64_t probe_expect_tx_ok() {
    ConstevalPlanFixture fixture{};
    return derive_expect_tx<256>(fixture.plan, ::crucible::SlotId{0u}).into();
}
static_assert(probe_expect_tx_ok() == 256u);

}  // namespace crucible::fixy::async_pipeline::detail::self_test
