#pragma once

// CNT-P application-level path swap.
//
// GAPS-122 owns the transport-independent handoff protocol: drain the
// old path, accept both paths during the handoff, then atomically move a
// live session handle to a new resource at the same protocol position.
// Kernel MPTCP subflow management, RDMA QP migration, route selection,
// and socket creation are separate transport substrates.
//
// fixy-A5-024 STATE-ONLY HONESTY MARKER.  `PathSwapper` ships the
// state-machine `Stable → Draining → BidirReceive → NewPathFlushing
// → Complete` correctly but performs ZERO in-flight data migration.
// `commit_sender` detaches the old session via
// `TransportClosedOutOfBand` and mints a fresh handle on the new
// resource — any bytes buffered on the OLD path are LOST.  Real
// transport-class-specific reconnect (MPTCP subflow swap, RDMA QP
// recovery, AF_XDP umem rebind) is downstream work tracked separately.
//
// The static constant `PathSwapper::data_migration_implemented` is
// the grep-discoverable single-source-of-truth: callers branching on
// it can detect the gap at compile time.  When per-transport engines
// land, that constant flips to `true` per-specialization AND the
// commit_sender doc warning is removed in the same PR.

#include <crucible/effects/Capabilities.h>
#include <crucible/effects/EffectRow.h>
#include <crucible/effects/ExecCtx.h>
#include <crucible/safety/Pinned.h>
#include <crucible/safety/Refined.h>
#include <crucible/safety/Tagged.h>
#include <crucible/sessions/Session.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::cntp {

enum class SwapState : std::uint8_t {
    Stable = 0,
    Draining = 1,
    BidirReceive = 2,
    NewPathFlushing = 3,
    Complete = 4,
    Failed = 5,
};

enum class SwapError : std::uint8_t {
    InvalidPathId,
    SamePath,
    DeadlineOverflow,
    Timeout,
    InvalidTransition,
};

[[nodiscard]] std::string_view swap_state_name(SwapState state) noexcept;
[[nodiscard]] std::string_view swap_error_name(SwapError error) noexcept;

using PositivePathId = safety::Positive<std::uint64_t>;
using PositiveNanoseconds = safety::Positive<std::uint64_t>;

struct PathSwapPlan {
    PositivePathId flow_id{1};
    PositivePathId old_path{1};
    PositivePathId new_path{2};
    PositiveNanoseconds timeout_ns{10'000'000'000ull};
};

using DeclaredPathSwapPlan =
    safety::Tagged<PathSwapPlan, safety::source::PathSwap>;

struct PathSwapEvent {
    std::uint64_t flow_id = 0;
    std::uint64_t old_path = 0;
    std::uint64_t new_path = 0;
    SwapState from = SwapState::Stable;
    SwapState to = SwapState::Stable;
    std::uint64_t at_ns = 0;
    std::uint64_t sequence = 0;
};

static_assert(sizeof(PositivePathId) == sizeof(std::uint64_t));
static_assert(sizeof(PositiveNanoseconds) == sizeof(std::uint64_t));
static_assert(sizeof(DeclaredPathSwapPlan) == sizeof(PathSwapPlan));
static_assert(std::is_trivially_copyable_v<PathSwapPlan>);
static_assert(std::is_trivially_copyable_v<PathSwapEvent>);

template <class Ctx>
concept CtxFitsPathSwapMint =
       effects::IsExecCtx<Ctx>
    && effects::CtxOwnsCapability<Ctx, effects::Effect::Init>;

template <class Ctx>
concept CtxFitsPathSwapTransition =
       effects::IsExecCtx<Ctx>
    && effects::CtxOwnsCapability<Ctx, effects::Effect::Bg>;

template <class Resource>
concept PathSwapSessionResource =
    safety::proto::SessionResource<Resource>;

[[nodiscard]] constexpr std::expected<PositivePathId, SwapError>
admit_path_id(std::uint64_t id) noexcept {
    if (id == 0) {
        return std::unexpected(SwapError::InvalidPathId);
    }
    return PositivePathId{id, typename PositivePathId::Trusted{}};
}

[[nodiscard]] constexpr std::expected<PositiveNanoseconds, SwapError>
admit_swap_timeout_ns(std::uint64_t ns) noexcept {
    if (ns == 0) {
        return std::unexpected(SwapError::Timeout);
    }
    return PositiveNanoseconds{ns, typename PositiveNanoseconds::Trusted{}};
}

[[nodiscard]] constexpr std::expected<DeclaredPathSwapPlan, SwapError>
mint_path_swap_plan(PositivePathId flow_id,
                    PositivePathId old_path,
                    PositivePathId new_path,
                    PositiveNanoseconds timeout_ns) noexcept {
    if (old_path.value() == new_path.value()) {
        return std::unexpected(SwapError::SamePath);
    }
    return DeclaredPathSwapPlan{PathSwapPlan{
        .flow_id = flow_id,
        .old_path = old_path,
        .new_path = new_path,
        .timeout_ns = timeout_ns,
    }};
}

template <std::size_t MaxEvents = 16>
class PathSwapper : public safety::Pinned<PathSwapper<MaxEvents>> {
    static_assert(MaxEvents > 0, "PathSwapper requires an audit-event ring");
    static_assert(std::atomic<SwapState>::is_always_lock_free,
                  "PathSwapper observers need a lock-free state load on every "
                  "supported platform; the target ISA does not provide one");

 public:
    // fixy-A5-024 honesty marker.  False until a per-transport engine
    // ships actual MPTCP/RDMA/AF_XDP in-flight migration; callers can
    // branch on this at compile time to detect the gap.
    static constexpr bool data_migration_implemented = false;

 private:

    // fixy-A5-038: state_ is published by a single Bg-context writer and
    // observed concurrently by foreground threads through state(). The
    // Pinned base advertises address-stable cross-thread sharing — the
    // field must be atomic to back that contract.  Plain `SwapState`
    // would tear under concurrent transition_to/state() races.
    std::atomic<SwapState> state_{SwapState::Stable};
    PathSwapPlan plan_{};
    std::array<PathSwapEvent, MaxEvents> events_{};
    std::size_t next_event_ = 0;
    std::size_t event_count_ = 0;
    std::uint64_t deadline_ns_ = 0;
    std::uint64_t sequence_ = 0;

    constexpr void append_event(SwapState from,
                                SwapState to,
                                std::uint64_t at_ns) noexcept {
        ++sequence_;
        events_[next_event_] = PathSwapEvent{
            .flow_id = plan_.flow_id.value(),
            .old_path = plan_.old_path.value(),
            .new_path = plan_.new_path.value(),
            .from = from,
            .to = to,
            .at_ns = at_ns,
            .sequence = sequence_,
        };
        next_event_ = (next_event_ + 1u) % MaxEvents;
        if (event_count_ < MaxEvents) {
            ++event_count_;
        }
    }

    void transition_to(SwapState next, std::uint64_t at_ns) noexcept {
        const SwapState prev = state_.load(std::memory_order_relaxed);
        state_.store(next, std::memory_order_release);
        append_event(prev, next, at_ns);
    }

    [[nodiscard]] bool expired(std::uint64_t now_ns) const noexcept {
        const SwapState cur = state_.load(std::memory_order_acquire);
        return cur != SwapState::Stable &&
               cur != SwapState::Complete &&
               cur != SwapState::Failed &&
               now_ns > deadline_ns_;
    }

    [[nodiscard]] std::expected<void, SwapError>
    check_live(std::uint64_t now_ns) noexcept {
        if (expired(now_ns)) {
            transition_to(SwapState::Failed, now_ns);
            return std::unexpected(SwapError::Timeout);
        }
        return {};
    }

public:
    constexpr PathSwapper() noexcept = default;

    [[nodiscard]] SwapState state() const noexcept {
        return state_.load(std::memory_order_acquire);
    }
    [[nodiscard]] constexpr PathSwapPlan plan() const noexcept { return plan_; }
    [[nodiscard]] constexpr std::uint64_t deadline_ns() const noexcept {
        return deadline_ns_;
    }
    [[nodiscard]] constexpr std::uint64_t sequence() const noexcept {
        return sequence_;
    }
    [[nodiscard]] constexpr std::size_t event_count() const noexcept {
        return event_count_;
    }

    [[nodiscard]] constexpr PathSwapEvent event_at(std::size_t index) const noexcept {
        return events_[index % MaxEvents];
    }

    template <class Ctx>
        requires CtxFitsPathSwapTransition<Ctx>
    [[nodiscard]] std::expected<void, SwapError>
    begin_swap(Ctx const&,
               DeclaredPathSwapPlan plan,
               std::uint64_t now_ns) noexcept {
        const SwapState cur = state_.load(std::memory_order_acquire);
        if (cur != SwapState::Stable && cur != SwapState::Complete) {
            return std::unexpected(SwapError::InvalidTransition);
        }
        auto const& raw = plan.value();
        if (raw.timeout_ns.value() >
            std::numeric_limits<std::uint64_t>::max() - now_ns) {
            return std::unexpected(SwapError::DeadlineOverflow);
        }
        plan_ = raw;
        deadline_ns_ = now_ns + raw.timeout_ns.value();
        transition_to(SwapState::Draining, now_ns);
        return {};
    }

    template <class Ctx>
        requires CtxFitsPathSwapTransition<Ctx>
    [[nodiscard]] std::expected<void, SwapError>
    receiver_accepts_bidir(Ctx const&, std::uint64_t now_ns) noexcept {
        if (auto live = check_live(now_ns); !live.has_value()) {
            return live;
        }
        if (state_.load(std::memory_order_acquire) != SwapState::Draining) {
            return std::unexpected(SwapError::InvalidTransition);
        }
        transition_to(SwapState::BidirReceive, now_ns);
        return {};
    }

    template <class Ctx>
        requires CtxFitsPathSwapTransition<Ctx>
    [[nodiscard]] std::expected<void, SwapError>
    sender_observed_drain_ack(Ctx const&, std::uint64_t now_ns) noexcept {
        if (auto live = check_live(now_ns); !live.has_value()) {
            return live;
        }
        if (state_.load(std::memory_order_acquire) != SwapState::BidirReceive) {
            return std::unexpected(SwapError::InvalidTransition);
        }
        transition_to(SwapState::NewPathFlushing, now_ns);
        return {};
    }

    // fixy-A5-024: STATE-MACHINE-ONLY commit, no in-flight data migration.
    //
    // This call:
    //   1. Detaches `current` with `TransportClosedOutOfBand{}`.  Any bytes
    //      still buffered on the OLD path (kernel TX queue, NIC ring, in-
    //      flight datagrams, app-level send window) are LOST — there is no
    //      replay, no resend, no buffer hand-off.  Receivers MUST tolerate
    //      this (or use an application-level ACK/idempotency layer).
    //   2. Transitions Tier-1 state to `Complete`.
    //   3. Mints a fresh `SessionHandle` over `new_resource` and returns it.
    //
    // The Tier-2 per-transport migration engines (MPTCP TCP_MIGRATE, RDMA
    // path-migration verb, AF_XDP map swap) that would preserve in-flight
    // bytes are intentionally NOT implemented here — see the class-level
    // `data_migration_implemented = false` honesty marker.  Callers can
    // branch on that constexpr to detect the gap at compile time.
    template <class Ctx,
              typename Proto,
              typename OldResource,
              typename LoopCtx,
              typename NewResource>
        requires CtxFitsPathSwapTransition<Ctx> &&
                 PathSwapSessionResource<NewResource>
    [[nodiscard]] auto commit_sender(
        Ctx const&,
        safety::proto::SessionHandle<Proto, OldResource, LoopCtx>&& current,
        NewResource&& new_resource,
        std::uint64_t now_ns) noexcept
        -> std::expected<
            safety::proto::SessionHandle<Proto, NewResource, LoopCtx>,
            SwapError>
    {
        if (auto live = check_live(now_ns); !live.has_value()) {
            return std::unexpected(live.error());
        }
        if (state_.load(std::memory_order_acquire) != SwapState::NewPathFlushing) {
            return std::unexpected(SwapError::InvalidTransition);
        }

        // STATE-ONLY: in-flight bytes on `current` are dropped on detach;
        // see fixy-A5-024 doc-block above.
        std::move(current).detach(
            safety::proto::detach_reason::TransportClosedOutOfBand{});
        transition_to(SwapState::Complete, now_ns);
        return safety::proto::mint_session_handle<Proto, NewResource>(
            std::forward<NewResource>(new_resource));
    }

    template <class Ctx>
        requires CtxFitsPathSwapTransition<Ctx>
    [[nodiscard]] std::expected<void, SwapError>
    complete_receiver(Ctx const&, std::uint64_t now_ns) noexcept {
        if (auto live = check_live(now_ns); !live.has_value()) {
            return live;
        }
        const SwapState cur = state_.load(std::memory_order_acquire);
        if (cur != SwapState::BidirReceive &&
            cur != SwapState::NewPathFlushing) {
            return std::unexpected(SwapError::InvalidTransition);
        }
        transition_to(SwapState::Complete, now_ns);
        return {};
    }
};

template <std::size_t MaxEvents = 16, class Ctx>
    requires CtxFitsPathSwapMint<Ctx>
[[nodiscard]] constexpr PathSwapper<MaxEvents>
mint_path_swapper(Ctx const&) noexcept {
    return {};
}

static_assert(CtxFitsPathSwapMint<effects::ColdInitCtx>);
static_assert(!CtxFitsPathSwapMint<effects::BgDrainCtx>);
static_assert(CtxFitsPathSwapTransition<effects::BgDrainCtx>);
static_assert(!CtxFitsPathSwapTransition<effects::HotFgCtx>);

}  // namespace crucible::cntp
