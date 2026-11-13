#pragma once

// An application-level move of one flow from an old path to a new one.  A
// background state machine drives the swap, and its last step hands the
// session to a new transport resource.  No in-flight data moves with it.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/Refined.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>
#include <fixy/os/SpinLock.h>
#include <fixy/session/Handle.h>
#include <fixy/session/Mint.h>
#include <foundation/Pinned.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>
#include <foundation/effects/Row.h>
#include <foundation/permissions/Permission.h>

#include <cstddef>
#include <cstdint>
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
[[nodiscard]] constexpr ::fixy::TextView swap_state_name(SwapState state) noexcept {
    return ::foundation::core::enum_name_text(state);
}

[[nodiscard]] constexpr ::fixy::TextView swap_error_name(SwapError error) noexcept {
    return ::foundation::core::enum_name_text(error);
}

using PositivePathId = ::fixy::Positive<std::uint64_t>;
using PositiveNanoseconds = ::fixy::Positive<std::uint64_t>;

class PathSwapPlan;
class PathSwapPlanDoor;

using DeclaredPathSwapPlan = ::fixy::Tagged<PathSwapPlan, ::fixy::tags::source::PathSwap>;

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

    friend class PathSwapPlanDoor;
};

[[nodiscard]] constexpr ::fixy::Result<DeclaredPathSwapPlan, SwapError>
mint_path_swap_plan(PositivePathId flow_id, PositivePathId old_path, PositivePathId new_path,
                    PositiveNanoseconds timeout_ns) noexcept;

// The constructor of a plan.  Its one member is private, and the mint is
// its one friend.  The friend names the Result of a plan, so it stands after
// the plan is complete.
class PathSwapPlanDoor {
    [[nodiscard]] static constexpr PathSwapPlan build(PositivePathId flow_id, PositivePathId old_path,
                                                      PositivePathId new_path,
                                                      PositiveNanoseconds timeout_ns) noexcept {
        return PathSwapPlan{flow_id, old_path, new_path, timeout_ns};
    }

    friend constexpr ::fixy::Result<DeclaredPathSwapPlan, SwapError>
        mint_path_swap_plan(PositivePathId, PositivePathId, PositivePathId, PositiveNanoseconds) noexcept;
};

[[nodiscard]] constexpr ::fixy::Result<DeclaredPathSwapPlan, SwapError>
mint_path_swap_plan(PositivePathId flow_id, PositivePathId old_path, PositivePathId new_path,
                    PositiveNanoseconds timeout_ns) noexcept {
    if (old_path == new_path) {
        return ::fixy::err(SwapError::SamePath);
    }
    return ::fixy::mint_tagged<::fixy::tags::source::PathSwap>(
        PathSwapPlanDoor::build(flow_id, old_path, new_path, timeout_ns));
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

// A swapper is built at startup.
template <class Ctx>
concept CtxFitsPathSwapMint = ::foundation::effects::CtxOwnsCapability<Ctx, ::foundation::effects::Effect::Init>;

// A transition is background work, and it waits on the gate of the swapper.
// A wait on that gate is a block, so the context also owns Block.
template <class Ctx>
concept CtxFitsPathSwapTransition =
    ::foundation::effects::CtxOwnsCapability<Ctx, ::foundation::effects::Effect::Bg> && ::fixy::spin::CtxMayBlock<Ctx>;

// A read of the plan, the deadline or the audit log waits on the same gate.
template <class Ctx>
concept CtxFitsPathSwapRead = ::fixy::spin::CtxMayBlock<Ctx>;

template <class Resource>
concept PathSwapSessionResource = ::fixy::session::SessionResource<Resource>;

[[nodiscard]] constexpr ::fixy::Result<PositivePathId, SwapError> admit_path_id(std::uint64_t id) noexcept {
    return ::fixy::admit_refined<::fixy::positive>(id, SwapError::InvalidPathId);
}

[[nodiscard]] constexpr ::fixy::Result<PositiveNanoseconds, SwapError>
admit_swap_timeout_ns(std::uint64_t ns) noexcept {
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

    using EventIndex = ::fixy::Index<MaxEvents>;

public:
    // False: commit_sender moves the protocol position to a new resource but
    // migrates no in-flight data.
    static constexpr bool data_migration_implemented = false;

private:
    // Each acquisition of the gate mints a token of this tag.  The tag is
    // private and nested in the class template, so only this instantiation
    // can mint one: the token witnesses that the acquisition comes from
    // inside the swapper.
    struct GateTag {
        using permission_row = ::foundation::effects::Row<>;
    };

    // The gate serializes each transition and each read of the fields below
    // state_.  So one thread at a time writes the plan, the deadline and the
    // audit log, and a reader copies a whole plan or a whole event.  Without
    // the gate, two threads that start a swap at the same time can each write
    // the plan, and the loser can replace the plan of the swap that won.
    mutable ::fixy::spin::BlockingLock<GateTag> gate_{};
    // Only the holder of the gate writes the state, and state() reads it
    // without the gate, so an observer never waits on a transition.
    ::fixy::Atomic<SwapState> state_{SwapState::Stable};
    // Empty until the first begin_swap.  Every transition that records an
    // event comes after that call, because Draining is the only state that
    // Stable leads to and only begin_swap enters it.
    ::fixy::Option<PathSwapPlan> plan_{};
    ::foundation::core::FixedArray<PathSwapEvent, MaxEvents> events_{};
    EventIndex next_event_ = ::fixy::index<0>();
    std::size_t event_count_ = 0;
    std::uint64_t deadline_ns_ = 0;
    std::uint64_t sequence_ = 0;

    // The mint is the only door, so a swapper exists only where a context
    // that owns Init built it.
    constexpr PathSwapper() noexcept = default;

    template <std::size_t N, class Ctx>
        requires CtxFitsPathSwapMint<Ctx>
    friend constexpr PathSwapper<N> mint_path_swapper(Ctx const& ctx) noexcept;

    // The caller holds the gate, and begin_swap set the plan.  The ring
    // gives each event the slot after the slot of the event before it.
    void append_event(SwapState from, SwapState to, std::uint64_t at_ns) noexcept {
        ++sequence_;
        PathSwapEvent event{.from = from, .to = to, .at_ns = at_ns, .sequence = sequence_};
        plan_.match(
            [&event](PathSwapPlan const& plan) noexcept {
                event.flow_id = plan.flow_id().value();
                event.old_path = plan.old_path().value();
                event.new_path = plan.new_path().value();
            },
            []() noexcept { ::fixy::fatal("an event of a swap comes after begin_swap, which sets the plan"); });
        events_.at(next_event_) = event;
        next_event_ = EventIndex::check((next_event_.raw() + 1u) % MaxEvents)
                          .expect("a remainder of a division by MaxEvents is below MaxEvents");
        if (event_count_ < MaxEvents) {
            ++event_count_;
        }
    }

    // The caller holds the gate, so no other transition runs until the
    // state is stored.  The event lands before the state, so a reader that
    // sees a state and then takes the gate finds the event of that state.
    //
    // Returns false when next is not a valid successor of the current
    // state.  The public methods turn that into SwapError::InvalidTransition.
    //
    // A consumed-by-value typestate token would put this DAG in the type
    // system, but state() reads the state concurrently with transitions, and
    // a token consumed on transition cannot be shared with readers.
    [[nodiscard]] bool transition_to(SwapState next, std::uint64_t at_ns) noexcept {
        const SwapState prev = state_.load_acquire();
        if (!is_valid_path_swap_transition(prev, next)) {
            return false;
        }
        append_event(prev, next, at_ns);
        state_.store_release(next);
        return true;
    }

    // The caller holds the gate.
    [[nodiscard]] bool expired(std::uint64_t now_ns) const noexcept {
        const SwapState cur = state_.load_acquire();
        return cur != SwapState::Stable && cur != SwapState::Complete && cur != SwapState::Failed
            && now_ns > deadline_ns_;
    }

    // The caller holds the gate.  A live swap past its deadline fails, and
    // the call that saw the deadline pass returns Timeout.
    [[nodiscard]] ::fixy::Result<::fixy::Unit, SwapError> check_live(std::uint64_t now_ns) noexcept {
        if (expired(now_ns)) {
            // Each live state leads to Failed, so the transition holds.
            (void)transition_to(SwapState::Failed, now_ns);
            return ::fixy::err(SwapError::Timeout);
        }
        return ::fixy::Unit{};
    }

    // The caller holds the gate.  Moves a live swap from `from` to `to`.
    [[nodiscard]] ::fixy::Result<::fixy::Unit, SwapError> advance(SwapState from, SwapState to,
                                                                  std::uint64_t now_ns) noexcept {
        CRUCIBLE_TRY(_, check_live(now_ns));
        if (state_.load_acquire() != from || !transition_to(to, now_ns)) {
            return ::fixy::err(SwapError::InvalidTransition);
        }
        return ::fixy::Unit{};
    }

public:
    [[nodiscard]] SwapState state() const noexcept { return state_.load_acquire(); }

    template <class Ctx>
        requires CtxFitsPathSwapRead<Ctx>
    // A copy of the plan.  An Option of a plan is move-only, so the copy
    // comes from a copy of the plan itself.
    [[nodiscard]] ::fixy::Option<PathSwapPlan> plan(Ctx const& ctx) const noexcept {
        auto proof = ::foundation::permissions::mint_permission_root<GateTag>();
        ::fixy::spin::GateGuard guard{ctx, gate_, proof};
        return plan_.match([](PathSwapPlan const& plan) noexcept { return ::fixy::some(plan); },
                           []() noexcept { return ::fixy::Option<PathSwapPlan>{::fixy::none}; });
    }

    template <class Ctx>
        requires CtxFitsPathSwapRead<Ctx>
    [[nodiscard]] std::uint64_t deadline_ns(Ctx const& ctx) const noexcept {
        auto proof = ::foundation::permissions::mint_permission_root<GateTag>();
        ::fixy::spin::GateGuard guard{ctx, gate_, proof};
        return deadline_ns_;
    }

    template <class Ctx>
        requires CtxFitsPathSwapRead<Ctx>
    [[nodiscard]] std::uint64_t sequence(Ctx const& ctx) const noexcept {
        auto proof = ::foundation::permissions::mint_permission_root<GateTag>();
        ::fixy::spin::GateGuard guard{ctx, gate_, proof};
        return sequence_;
    }

    template <class Ctx>
        requires CtxFitsPathSwapRead<Ctx>
    [[nodiscard]] std::size_t event_count(Ctx const& ctx) const noexcept {
        auto proof = ::foundation::permissions::mint_permission_root<GateTag>();
        ::fixy::spin::GateGuard guard{ctx, gate_, proof};
        return event_count_;
    }

    // The ring holds the event with sequence number s at index s - 1,
    // modulo MaxEvents.
    template <class Ctx>
        requires CtxFitsPathSwapRead<Ctx>
    [[nodiscard]] PathSwapEvent event_at(Ctx const& ctx, std::size_t index) const noexcept {
        auto proof = ::foundation::permissions::mint_permission_root<GateTag>();
        ::fixy::spin::GateGuard guard{ctx, gate_, proof};
        return events_.at(
            EventIndex::check(index % MaxEvents).expect("a remainder of a division by MaxEvents is below MaxEvents"));
    }

    template <class Ctx>
        requires CtxFitsPathSwapTransition<Ctx>
    [[nodiscard]] ::fixy::Result<::fixy::Unit, SwapError> begin_swap(Ctx const& ctx, DeclaredPathSwapPlan const& plan,
                                                                     std::uint64_t now_ns) noexcept {
        auto proof = ::foundation::permissions::mint_permission_root<GateTag>();
        ::fixy::spin::GateGuard guard{ctx, gate_, proof};
        const SwapState cur = state_.load_acquire();
        if (cur != SwapState::Stable && cur != SwapState::Complete) {
            return ::fixy::err(SwapError::InvalidTransition);
        }
        PathSwapPlan const& raw = plan.value();
        CRUCIBLE_TRY(
            deadline,
            ::foundation::core::checked_add(now_ns, raw.timeout_ns().value()).ok_or(SwapError::DeadlineOverflow));
        plan_ = ::fixy::some(raw);
        deadline_ns_ = deadline;
        // Stable and Complete each lead to Draining, so the transition holds.
        (void)transition_to(SwapState::Draining, now_ns);
        return ::fixy::Unit{};
    }

    template <class Ctx>
        requires CtxFitsPathSwapTransition<Ctx>
    [[nodiscard]] ::fixy::Result<::fixy::Unit, SwapError> receiver_accepts_bidir(Ctx const& ctx,
                                                                                 std::uint64_t now_ns) noexcept {
        auto proof = ::foundation::permissions::mint_permission_root<GateTag>();
        ::fixy::spin::GateGuard guard{ctx, gate_, proof};
        return advance(SwapState::Draining, SwapState::BidirReceive, now_ns);
    }

    template <class Ctx>
        requires CtxFitsPathSwapTransition<Ctx>
    [[nodiscard]] ::fixy::Result<::fixy::Unit, SwapError> sender_observed_drain_ack(Ctx const& ctx,
                                                                                    std::uint64_t now_ns) noexcept {
        auto proof = ::foundation::permissions::mint_permission_root<GateTag>();
        ::fixy::spin::GateGuard guard{ctx, gate_, proof};
        return advance(SwapState::BidirReceive, SwapState::NewPathFlushing, now_ns);
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
    auto commit_sender(Ctx const& ctx, ::fixy::session::SessionHandle<Proto, OldResource, void, Policy>&& current,
                       NewResource&& new_resource, std::uint64_t now_ns) noexcept
        -> ::fixy::Result<::fixy::session::SessionHandle<Proto, NewResource, void, Policy>, SwapError> {
        // The transition has to land before the detach.  A call that is
        // refused must not consume `current`, or the resource leaks and the
        // audit log gains an event for a transition that never happened.
        // The detach and the new mint run after the gate is released.
        {
            auto proof = ::foundation::permissions::mint_permission_root<GateTag>();
            ::fixy::spin::GateGuard guard{ctx, gate_, proof};
            CRUCIBLE_TRY(_, advance(SwapState::NewPathFlushing, SwapState::Complete, now_ns));
        }
        std::move(current).detach(::fixy::session::detach_reason::TransportClosedOutOfBand{});
        return ::fixy::session::mint_session_handle<Proto, NewResource, Policy>(
            std::forward<NewResource>(new_resource));
    }

    template <class Ctx>
        requires CtxFitsPathSwapTransition<Ctx>
    [[nodiscard]] ::fixy::Result<::fixy::Unit, SwapError> complete_receiver(Ctx const& ctx,
                                                                            std::uint64_t now_ns) noexcept {
        auto proof = ::foundation::permissions::mint_permission_root<GateTag>();
        ::fixy::spin::GateGuard guard{ctx, gate_, proof};
        CRUCIBLE_TRY(_, check_live(now_ns));
        // No check of the current state comes first.  Complete is not a
        // valid successor of Complete, so the second of two callers is
        // refused.
        if (!transition_to(SwapState::Complete, now_ns)) {
            return ::fixy::err(SwapError::InvalidTransition);
        }
        return ::fixy::Unit{};
    }
};

template <std::size_t MaxEvents, class Ctx>
    requires CtxFitsPathSwapMint<Ctx>
[[nodiscard]] constexpr PathSwapper<MaxEvents> mint_path_swapper(Ctx const&) noexcept {
    return PathSwapper<MaxEvents>{};
}

}  // namespace crucible::cntp
