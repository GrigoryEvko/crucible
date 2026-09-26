#pragma once

// An application-level move of one flow from an old path to a new one.  A
// background state machine drives the swap, and its last step hands the
// session to a new transport resource.  No in-flight data moves with it.

#include <fixy/Ctx.h>
#include <fixy/Refined.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>
#include <fixy/session/Handle.h>
#include <foundation/Pinned.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>
#include <foundation/reflect/EnumName.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <optional>
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

// Complete accepts Draining so one swapper can serve a second swap.  Failed
// is fully terminal: recovery means discarding the swapper and minting a
// fresh one.
[[nodiscard]] constexpr bool is_valid_path_swap_transition(SwapState from, SwapState to) noexcept {
    switch (from) {
        case SwapState::Stable:
            return to == SwapState::Draining;
        case SwapState::Complete:
            return to == SwapState::Draining;
        case SwapState::Draining:
            return to == SwapState::BidirReceive || to == SwapState::Failed;
        case SwapState::BidirReceive:
            return to == SwapState::NewPathFlushing || to == SwapState::Complete || to == SwapState::Failed;
        case SwapState::NewPathFlushing:
            return to == SwapState::Complete || to == SwapState::Failed;
        case SwapState::Failed:
            return false;
        default:
            // Unreachable for a valid SwapState.  The arm exists because
            // switch-default is required, and a value cast in from untrusted
            // bytes lands here and is refused.
            return false;
    }
}

enum class SwapError : std::uint8_t {
    InvalidPathId,
    SamePath,
    DeadlineOverflow,
    Timeout,
    InvalidTransition,
};

// The name of a state or of an error is the identifier of its enumerator.
[[nodiscard]] constexpr std::string_view swap_state_name(SwapState state) noexcept {
    return ::foundation::reflect::enum_name(state);
}

[[nodiscard]] constexpr std::string_view swap_error_name(SwapError error) noexcept {
    return ::foundation::reflect::enum_name(error);
}

using PositivePathId = ::fixy::Positive<std::uint64_t>;
using PositiveNanoseconds = ::fixy::Positive<std::uint64_t>;

class PathSwapPlan;

using DeclaredPathSwapPlan = ::fixy::Tagged<PathSwapPlan, ::fixy::tags::source::PathSwap>;

[[nodiscard]] constexpr std::expected<DeclaredPathSwapPlan, SwapError>
mint_path_swap_plan(PositivePathId flow_id, PositivePathId old_path, PositivePathId new_path,
                    PositiveNanoseconds timeout_ns) noexcept;

// A plan moves one flow from one path to a different path before a
// deadline.  The constructor is private and mint_path_swap_plan is its only
// caller, so every plan names two different paths.  There is no default
// plan: a plan that nobody declared would move a flow nobody named.
class PathSwapPlan {
public:
    [[nodiscard]] constexpr PositivePathId flow_id() const noexcept { return flow_id_; }
    [[nodiscard]] constexpr PositivePathId old_path() const noexcept { return old_path_; }
    [[nodiscard]] constexpr PositivePathId new_path() const noexcept { return new_path_; }
    [[nodiscard]] constexpr PositiveNanoseconds timeout_ns() const noexcept { return timeout_ns_; }

private:
    constexpr PathSwapPlan(PositivePathId flow_id, PositivePathId old_path, PositivePathId new_path,
                           PositiveNanoseconds timeout_ns) noexcept
        : flow_id_{flow_id}, old_path_{old_path}, new_path_{new_path}, timeout_ns_{timeout_ns} {}

    PositivePathId flow_id_;
    PositivePathId old_path_;
    PositivePathId new_path_;
    PositiveNanoseconds timeout_ns_;

    friend constexpr std::expected<DeclaredPathSwapPlan, SwapError>
    mint_path_swap_plan(PositivePathId, PositivePathId, PositivePathId, PositiveNanoseconds) noexcept;
};

[[nodiscard]] constexpr std::expected<DeclaredPathSwapPlan, SwapError>
mint_path_swap_plan(PositivePathId flow_id, PositivePathId old_path, PositivePathId new_path,
                    PositiveNanoseconds timeout_ns) noexcept {
    if (old_path == new_path) {
        return std::unexpected(SwapError::SamePath);
    }
    return ::fixy::mint_tagged<::fixy::tags::source::PathSwap>(PathSwapPlan{flow_id, old_path, new_path, timeout_ns});
}

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
static_assert(!std::is_default_constructible_v<PathSwapPlan> && !std::is_aggregate_v<PathSwapPlan>,
              "mint_path_swap_plan must be the only door to a plan");
// A refined member makes the plan not trivially copyable, because no byte
// route may build a refined value.  A copy still costs what a copy of the
// bytes costs.
static_assert(std::is_trivially_copy_constructible_v<PathSwapPlan> && std::is_trivially_destructible_v<PathSwapPlan>);
static_assert(std::is_trivially_copyable_v<PathSwapEvent>);

// A swapper is built at startup.
template <class Ctx>
concept CtxFitsPathSwapMint = ::foundation::effects::CtxOwnsCapability<Ctx, ::foundation::effects::Effect::Init>;

// A transition is background work.
template <class Ctx>
concept CtxFitsPathSwapTransition = ::foundation::effects::CtxOwnsCapability<Ctx, ::foundation::effects::Effect::Bg>;

template <class Resource>
concept PathSwapSessionResource = ::fixy::session::SessionResource<Resource>;

[[nodiscard]] constexpr std::expected<PositivePathId, SwapError> admit_path_id(std::uint64_t id) noexcept {
    return ::fixy::admit_refined<::fixy::positive>(id, SwapError::InvalidPathId);
}

[[nodiscard]] constexpr std::expected<PositiveNanoseconds, SwapError> admit_swap_timeout_ns(std::uint64_t ns) noexcept {
    return ::fixy::admit_refined<::fixy::positive>(ns, SwapError::Timeout);
}

template <std::size_t MaxEvents = 16>
class PathSwapper;

template <std::size_t MaxEvents = 16, class Ctx>
    requires CtxFitsPathSwapMint<Ctx>
[[nodiscard]] constexpr PathSwapper<MaxEvents> mint_path_swapper(Ctx const& ctx) noexcept;

template <std::size_t MaxEvents>
class PathSwapper : public ::foundation::Pinned<PathSwapper<MaxEvents>> {
    static_assert(MaxEvents > 0, "PathSwapper requires an audit-event ring");
    static_assert(std::atomic<SwapState>::is_always_lock_free,
                  "PathSwapper observers need a lock-free state load. The target ISA does not provide one");

public:
    // False: commit_sender moves the protocol position to a new resource but
    // migrates no in-flight data.
    static constexpr bool data_migration_implemented = false;

private:
    // One writer in a background context, many concurrent readers through
    // state().  The Pinned base advertises address-stable cross-thread
    // sharing, so this field has to be atomic to back that promise.
    std::atomic<SwapState> state_{SwapState::Stable};
    // Empty until the first begin_swap.  Every transition that records an
    // event comes after that call, because Draining is the only state that
    // Stable leads to and only begin_swap enters it.
    std::optional<PathSwapPlan> plan_{};
    std::array<PathSwapEvent, MaxEvents> events_{};
    std::size_t next_event_ = 0;
    std::size_t event_count_ = 0;
    std::uint64_t deadline_ns_ = 0;
    std::uint64_t sequence_ = 0;

    // The mint is the only door, so a swapper exists only where a context
    // that owns Init built it.
    constexpr PathSwapper() noexcept = default;

    template <std::size_t N, class Ctx>
        requires CtxFitsPathSwapMint<Ctx>
    friend constexpr PathSwapper<N> mint_path_swapper(Ctx const& ctx) noexcept;

    constexpr void append_event(SwapState from, SwapState to, std::uint64_t at_ns) noexcept {
        ++sequence_;
        events_[next_event_] = PathSwapEvent{
            .flow_id = plan_->flow_id().value(),
            .old_path = plan_->old_path().value(),
            .new_path = plan_->new_path().value(),
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

    // Validity is re-checked inside the loop, on the value the failed
    // compare_exchange loaded.  That is what makes exactly one thread observe
    // a given (prev, next) edge, so append_event records the real predecessor
    // rather than a stale load two threads both won.
    //
    // Returns false when another thread reached a state that is not a valid
    // predecessor of next.  The public methods turn that into
    // SwapError::InvalidTransition.  check_live ignores it, because losing
    // the race there means someone else already left the live path.
    //
    // A consumed-by-value typestate token would put this DAG in the type
    // system, but state(), expired() and event_at() all read the state
    // concurrently with transitions, and a token consumed on transition
    // cannot be shared with readers.
    [[nodiscard]] bool transition_to(SwapState next, std::uint64_t at_ns) noexcept {
        SwapState prev = state_.load(std::memory_order_acquire);
        do {
            if (!is_valid_path_swap_transition(prev, next)) {
                return false;
            }
        } while (!state_.compare_exchange_weak(prev, next, std::memory_order_acq_rel, std::memory_order_acquire));
        append_event(prev, next, at_ns);
        return true;
    }

    [[nodiscard]] bool expired(std::uint64_t now_ns) const noexcept {
        const SwapState cur = state_.load(std::memory_order_acquire);
        return cur != SwapState::Stable && cur != SwapState::Complete && cur != SwapState::Failed
            && now_ns > deadline_ns_;
    }

    [[nodiscard]] std::expected<void, SwapError> check_live(std::uint64_t now_ns) noexcept {
        if (expired(now_ns)) {
            // Losing this transition is benign.  It means another thread
            // already left the live path, so the deadline is moot either way
            // and Timeout still unwinds the current call.
            (void)transition_to(SwapState::Failed, now_ns);
            return std::unexpected(SwapError::Timeout);
        }
        return {};
    }

public:
    [[nodiscard]] SwapState state() const noexcept { return state_.load(std::memory_order_acquire); }
    [[nodiscard]] constexpr std::optional<PathSwapPlan> plan() const noexcept { return plan_; }
    [[nodiscard]] constexpr std::uint64_t deadline_ns() const noexcept { return deadline_ns_; }
    [[nodiscard]] constexpr std::uint64_t sequence() const noexcept { return sequence_; }
    [[nodiscard]] constexpr std::size_t event_count() const noexcept { return event_count_; }

    [[nodiscard]] constexpr PathSwapEvent event_at(std::size_t index) const noexcept {
        return events_[index % MaxEvents];
    }

    template <class Ctx>
        requires CtxFitsPathSwapTransition<Ctx>
    [[nodiscard]] std::expected<void, SwapError> begin_swap(Ctx const&, DeclaredPathSwapPlan const& plan,
                                                            std::uint64_t now_ns) noexcept {
        const SwapState cur = state_.load(std::memory_order_acquire);
        if (cur != SwapState::Stable && cur != SwapState::Complete) {
            return std::unexpected(SwapError::InvalidTransition);
        }
        PathSwapPlan const& raw = plan.value();
        if (raw.timeout_ns().value() > std::numeric_limits<std::uint64_t>::max() - now_ns) {
            return std::unexpected(SwapError::DeadlineOverflow);
        }
        plan_ = raw;
        deadline_ns_ = now_ns + raw.timeout_ns().value();
        if (!transition_to(SwapState::Draining, now_ns)) {
            return std::unexpected(SwapError::InvalidTransition);
        }
        return {};
    }

    template <class Ctx>
        requires CtxFitsPathSwapTransition<Ctx>
    [[nodiscard]] std::expected<void, SwapError> receiver_accepts_bidir(Ctx const&, std::uint64_t now_ns) noexcept {
        if (auto live = check_live(now_ns); !live.has_value()) {
            return live;
        }
        if (state_.load(std::memory_order_acquire) != SwapState::Draining) {
            return std::unexpected(SwapError::InvalidTransition);
        }
        if (!transition_to(SwapState::BidirReceive, now_ns)) {
            return std::unexpected(SwapError::InvalidTransition);
        }
        return {};
    }

    template <class Ctx>
        requires CtxFitsPathSwapTransition<Ctx>
    [[nodiscard]] std::expected<void, SwapError> sender_observed_drain_ack(Ctx const&, std::uint64_t now_ns) noexcept {
        if (auto live = check_live(now_ns); !live.has_value()) {
            return live;
        }
        if (state_.load(std::memory_order_acquire) != SwapState::BidirReceive) {
            return std::unexpected(SwapError::InvalidTransition);
        }
        if (!transition_to(SwapState::NewPathFlushing, now_ns)) {
            return std::unexpected(SwapError::InvalidTransition);
        }
        return {};
    }

    // Detaching `current` drops whatever is still buffered on the old path:
    // the kernel TX queue, the NIC ring, in-flight datagrams, the application
    // send window.  Nothing is replayed, resent or handed over.  A receiver
    // must tolerate that loss or sit behind its own idempotency layer.  The
    // deprecation makes each call site acknowledge the loss at compile time,
    // and data_migration_implemented above is the constant to test it by.
    //
    // The new handle is minted afresh at the position of `current`, so the
    // swap takes only a handle that such a mint can stand for: one outside a
    // loop body and with no permission set.  A handle that holds permissions
    // would lose them to the detach, so deduction refuses it.
    template <class Ctx, typename Proto, typename OldResource, ::fixy::session::AbandonmentPolicy Policy,
              typename NewResource>
        requires CtxFitsPathSwapTransition<Ctx> && PathSwapSessionResource<NewResource>
    [[nodiscard, deprecated("CRUCIBLE_STUB: commit_sender migrates no in-flight data. "
                            "The bytes buffered on the old path are dropped.")]]
    auto commit_sender(Ctx const&, ::fixy::session::SessionHandle<Proto, OldResource, void, Policy>&& current,
                       NewResource&& new_resource, std::uint64_t now_ns) noexcept
        -> std::expected<::fixy::session::SessionHandle<Proto, NewResource, void, Policy>, SwapError> {
        if (auto live = check_live(now_ns); !live.has_value()) {
            return std::unexpected(live.error());
        }
        if (state_.load(std::memory_order_acquire) != SwapState::NewPathFlushing) {
            return std::unexpected(SwapError::InvalidTransition);
        }
        // The transition has to land before the detach.  A thread that loses
        // the race must not consume `current`, or the resource leaks and the
        // audit log gains an event for a transition that never happened.
        if (!transition_to(SwapState::Complete, now_ns)) {
            return std::unexpected(SwapError::InvalidTransition);
        }
        std::move(current).detach(::fixy::session::detach_reason::TransportClosedOutOfBand{});
        return ::fixy::session::mint_session_handle<Proto, NewResource, Policy>(std::forward<NewResource>(new_resource));
    }

    template <class Ctx>
        requires CtxFitsPathSwapTransition<Ctx>
    [[nodiscard]] std::expected<void, SwapError> complete_receiver(Ctx const&, std::uint64_t now_ns) noexcept {
        if (auto live = check_live(now_ns); !live.has_value()) {
            return live;
        }
        // No pre-check on the current state is needed here.  Complete is not a
        // valid predecessor of Complete, so a second caller loses the CAS,
        // re-validates against the new state and is refused.
        if (!transition_to(SwapState::Complete, now_ns)) {
            return std::unexpected(SwapError::InvalidTransition);
        }
        return {};
    }
};

template <std::size_t MaxEvents, class Ctx>
    requires CtxFitsPathSwapMint<Ctx>
[[nodiscard]] constexpr PathSwapper<MaxEvents> mint_path_swapper(Ctx const&) noexcept {
    return PathSwapper<MaxEvents>{};
}

static_assert(CtxFitsPathSwapMint<::fixy::ColdInitCtx>);
static_assert(!CtxFitsPathSwapMint<::fixy::BgDrainCtx>);
static_assert(!CtxFitsPathSwapMint<::fixy::HotFgCtx>);
static_assert(CtxFitsPathSwapTransition<::fixy::BgDrainCtx>);
static_assert(!CtxFitsPathSwapTransition<::fixy::ColdInitCtx>);
static_assert(!CtxFitsPathSwapTransition<::fixy::HotFgCtx>);
static_assert(!std::is_default_constructible_v<PathSwapper<>>, "mint_path_swapper must be the only door to a swapper");

}  // namespace crucible::cntp
