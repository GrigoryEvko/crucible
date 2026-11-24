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
#include <fixy/session/Entry.h>
#include <fixy/session/Handle.h>
#include <foundation/Lifetime.h>
#include <foundation/Linear.h>
#include <foundation/Pinned.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>
#include <foundation/effects/Row.h>

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

// The roles of a value in a swap.  A state of a step is the state that the
// step leaves or the state that it enters, and a path of a plan is the old
// path or the new path.  Two roles give two types, so a call cannot take its
// two values of one role pair in the other order.
namespace side {

struct SwapFrom final {};
struct SwapTo final {};
struct OldPath final {};
struct NewPath final {};

}  // namespace side

using SwapFromState = ::fixy::Side<side::SwapFrom, SwapState>;
using SwapToState = ::fixy::Side<side::SwapTo, SwapState>;

// Complete accepts Draining so one swapper can serve a second swap.  Failed
// is fully terminal: recovery means discarding the swapper and minting a
// fresh one.
[[nodiscard]] constexpr bool is_valid_path_swap_transition(SwapFromState const& from, SwapToState const& to) noexcept {
    SwapState const next = to.value();
    switch (from.value()) {
        case SwapState::Stable:
            return next == SwapState::Draining;
        case SwapState::Complete:
            return next == SwapState::Draining;
        case SwapState::Draining:
            return next == SwapState::BidirReceive || next == SwapState::Failed;
        case SwapState::BidirReceive:
            return next == SwapState::NewPathFlushing || next == SwapState::Complete || next == SwapState::Failed;
        case SwapState::NewPathFlushing:
            return next == SwapState::Complete || next == SwapState::Failed;
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
    InvalidFlowId,
};

// The name of a state or of an error is the identifier of its enumerator.
[[nodiscard]] constexpr ::fixy::TextView swap_state_name(SwapState state) noexcept {
    return ::foundation::core::enum_name_text(state);
}

[[nodiscard]] constexpr ::fixy::TextView swap_error_name(SwapError error) noexcept {
    return ::foundation::core::enum_name_text(error);
}

// The kinds of the ids of a swap.  A flow and a path are two kinds, so the id
// of a flow cannot stand for the id of a path.
struct SwapFlowKind final {};
struct SwapPathKind final {};

using FlowId = ::fixy::Id<SwapFlowKind, std::uint64_t>;
using PathId = ::fixy::Id<SwapPathKind, std::uint64_t>;
using PositiveFlowId = ::fixy::Positive<FlowId>;
using PositivePathId = ::fixy::Positive<PathId>;

// A count of nanoseconds on the clock of the caller: the time of a call of
// a door, a deadline, or the timeout of a plan.  A timeout of a plan is above
// zero.
using SwapNanoseconds = ::fixy::Quantity<::fixy::unit::Nanosecond, std::uint64_t>;
using PositiveSwapTimeout = ::fixy::Positive<SwapNanoseconds>;

// The two paths of a plan in their two roles.
using OldPathId = ::fixy::Side<side::OldPath, PositivePathId>;
using NewPathId = ::fixy::Side<side::NewPath, PositivePathId>;

class PathSwapPlan;
class PathSwapPlanDoor;

using DeclaredPathSwapPlan = ::fixy::Tagged<PathSwapPlan, ::fixy::tags::source::PathSwap>;

// A plan moves one flow from one path to a different path before a
// deadline.  The constructor is private and mint_path_swap_plan is its only
// caller, so every plan names two different paths.  There is no default
// plan: a plan that nobody declared would move a flow nobody named.
class PathSwapPlan {
public:
    [[nodiscard]] constexpr PositiveFlowId flow_id() const noexcept { return flow_id_; }
    [[nodiscard]] constexpr PositivePathId old_path() const noexcept { return old_path_; }
    [[nodiscard]] constexpr PositivePathId new_path() const noexcept { return new_path_; }
    [[nodiscard]] constexpr PositiveSwapTimeout timeout() const noexcept { return timeout_; }

private:
    constexpr PathSwapPlan(PositiveFlowId flow_id, OldPathId const& old_path, NewPathId const& new_path,
                           PositiveSwapTimeout timeout) noexcept
        : flow_id_{flow_id}, old_path_{old_path.value()}, new_path_{new_path.value()}, timeout_{timeout} {}

    PositiveFlowId flow_id_;
    PositivePathId old_path_;
    PositivePathId new_path_;
    PositiveSwapTimeout timeout_;

    friend class PathSwapPlanDoor;
};

[[nodiscard]] constexpr ::fixy::Result<DeclaredPathSwapPlan, SwapError>
mint_path_swap_plan(PositiveFlowId flow_id, OldPathId const& old_path, NewPathId const& new_path,
                    PositiveSwapTimeout timeout) noexcept;

// The constructor of a plan.  Its one member is private, and the mint is
// its one friend.  The friend names the Result of a plan, so it stands after
// the plan is complete.
class PathSwapPlanDoor {
    [[nodiscard]] static constexpr PathSwapPlan build(PositiveFlowId flow_id, OldPathId const& old_path,
                                                      NewPathId const& new_path, PositiveSwapTimeout timeout) noexcept {
        return PathSwapPlan{flow_id, old_path, new_path, timeout};
    }

    friend constexpr ::fixy::Result<DeclaredPathSwapPlan, SwapError>
    mint_path_swap_plan(PositiveFlowId, OldPathId const&, NewPathId const&, PositiveSwapTimeout) noexcept;
};

// The plan that moves the flow from the old path to the new path, or
// SamePath when the two paths are one path.  Each parameter has a type of
// its own, so no two arguments swap at a call.
[[nodiscard]] constexpr ::fixy::Result<DeclaredPathSwapPlan, SwapError>
mint_path_swap_plan(PositiveFlowId flow_id, OldPathId const& old_path, NewPathId const& new_path,
                    PositiveSwapTimeout timeout) noexcept {
    if (old_path.value() == new_path.value()) {
        return ::fixy::err(SwapError::SamePath);
    }
    return ::fixy::mint_tagged<::fixy::tags::source::PathSwap>(
        PathSwapPlanDoor::build(flow_id, old_path, new_path, timeout));
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

// The refusal of a commit of a sender: the handle of the old path, which the
// swap did not take, and why.  The class is linear, so must-consume reads
// that the caller takes the handle back, or ends the refusal at a named door,
// on each path.  So no refused swap ends a session in silence.  The checked
// lifetime start of foundation/Lifetime.h refuses the class, so no start over
// bytes forges a refusal.
template <class Handle>
class [[nodiscard]][[= ::foundation::linear::linear{}]][[= ::foundation::lifetime::no_start_over_bytes{}]]
    SwapRefused final {
public:
    constexpr SwapRefused(Handle&& current, SwapError why) noexcept : current_{std::move(current)}, why_{why} {}

    constexpr SwapRefused(SwapRefused&&) noexcept = default;
    SwapRefused(SwapRefused const&) = delete("a refusal holds the one handle of the old path");
    SwapRefused& operator=(SwapRefused const&) = delete("a refusal holds the one handle of the old path");
    SwapRefused& operator=(SwapRefused&&) = delete("a refusal holds the one handle of the old path");
    ~SwapRefused() = default;

    // Why the swap did not take the handle.
    [[nodiscard]] constexpr SwapError reason() const noexcept { return why_; }

    // The handle back, at its position on the old path, for a later commit,
    // for its own protocol or for its own end.
    [[nodiscard]] constexpr Handle take_handle() && noexcept { return std::move(current_); }

private:
    Handle current_;
    SwapError why_ = SwapError::InvalidTransition;
};

// The admission of an id or a timeout from outside: the value above zero, or
// the error of its kind.
[[nodiscard]] constexpr ::fixy::Result<PositiveFlowId, SwapError> admit_flow_id(FlowId id) noexcept {
    return ::fixy::admit_refined<::fixy::positive>(id, SwapError::InvalidFlowId);
}

[[nodiscard]] constexpr ::fixy::Result<PositivePathId, SwapError> admit_path_id(PathId id) noexcept {
    return ::fixy::admit_refined<::fixy::positive>(id, SwapError::InvalidPathId);
}

[[nodiscard]] constexpr ::fixy::Result<PositiveSwapTimeout, SwapError>
admit_swap_timeout(SwapNanoseconds timeout) noexcept {
    return ::fixy::admit_refined<::fixy::positive>(timeout, SwapError::Timeout);
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
    // The tag of the lock of the guarded fields.  It is private and nested in
    // the class template, so only this instantiation names the lock.
    struct GateTag {};

    // The fields that only a body inside the lock reaches.
    struct Guarded {
        // Empty until the first begin_swap.  Every transition that records
        // an event comes after that call, because Draining is the only state
        // that Stable leads to and only begin_swap enters it.
        ::fixy::Option<PathSwapPlan> plan{};
        ::fixy::FixedArray<PathSwapEvent, MaxEvents> events{};
        EventIndex next_event = ::fixy::index<0>();
        std::size_t event_count = 0;
        SwapNanoseconds deadline{0};
        std::uint64_t sequence = 0;
    };

    // The lock serializes each transition and each read of the guarded
    // fields.  So one thread at a time writes the plan, the deadline and the
    // audit log, and a reader copies a whole plan or a whole event.  Without
    // the lock, two threads that start a swap at the same time can each write
    // the plan, and the loser can replace the plan of the swap that won.
    ::fixy::spin::BlockingLocked<Guarded, GateTag> guarded_{};
    // Only a body inside the lock writes the state, and state() reads it
    // without the lock, so an observer never waits on a transition.
    ::fixy::Atomic<SwapState> state_{SwapState::Stable};

    // The mint is the only door, so a swapper exists only where a context
    // that owns Init built it.
    constexpr PathSwapper() noexcept = default;

    template <std::size_t N, class Ctx>
        requires CtxFitsPathSwapMint<Ctx>
    friend constexpr PathSwapper<N> mint_path_swapper(Ctx const& ctx) noexcept;

    // The caller holds the lock and gives the guarded fields, and begin_swap
    // set the plan.  The ring gives each event the slot after the slot of
    // the event before it.
    static void append_event(Guarded& guarded, SwapFromState const& from, SwapToState const& to,
                             SwapNanoseconds at) noexcept {
        ++guarded.sequence;
        PathSwapEvent event{.from = from.value(), .to = to.value(), .at_ns = at.raw(), .sequence = guarded.sequence};
        guarded.plan.match(
            [&event](PathSwapPlan const& plan) noexcept {
                event.flow_id = plan.flow_id().value().raw();
                event.old_path = plan.old_path().value().raw();
                event.new_path = plan.new_path().value().raw();
            },
            []() noexcept { ::fixy::fatal("an event of a swap comes after begin_swap, which sets the plan"); });
        guarded.events.with_element(guarded.next_event, [&event](PathSwapEvent& slot) noexcept { slot = event; });
        guarded.next_event = guarded.next_event.next_cyclic();
        if (guarded.event_count < MaxEvents) {
            ++guarded.event_count;
        }
    }

    // The caller holds the lock and gives the guarded fields, so no other
    // transition runs until the state is stored.  The event lands before the
    // state, so a reader that sees a state and then takes the lock finds the
    // event of that state.
    //
    // Returns false when next is not a valid successor of the current
    // state.  The public methods turn that into SwapError::InvalidTransition.
    //
    // A consumed-by-value typestate token would put this DAG in the type
    // system, but state() reads the state concurrently with transitions, and
    // a token consumed on transition cannot be shared with readers.
    [[nodiscard]] bool transition_to(Guarded& guarded, SwapState next, SwapNanoseconds at) noexcept {
        SwapFromState const from{state_.load_acquire()};
        SwapToState const to{next};
        if (!is_valid_path_swap_transition(from, to)) {
            return false;
        }
        append_event(guarded, from, to, at);
        state_.store_release(next);
        return true;
    }

    // The caller holds the lock and gives the guarded fields.
    [[nodiscard]] bool expired(Guarded const& guarded, SwapNanoseconds now) const noexcept {
        const SwapState cur = state_.load_acquire();
        return cur != SwapState::Stable && cur != SwapState::Complete && cur != SwapState::Failed
            && now > guarded.deadline;
    }

    // The caller holds the lock and gives the guarded fields.  A live swap
    // past its deadline fails, and the call that saw the deadline pass
    // returns Timeout.
    [[nodiscard]] ::fixy::Result<::fixy::Unit, SwapError> check_live(Guarded& guarded, SwapNanoseconds now) noexcept {
        if (expired(guarded, now)) {
            // Each live state leads to Failed, so the transition holds.
            (void)transition_to(guarded, SwapState::Failed, now);
            return ::fixy::err(SwapError::Timeout);
        }
        return ::fixy::Unit{};
    }

    // The caller holds the lock and gives the guarded fields.  Moves a live
    // swap from `from` to `to`.
    [[nodiscard]] ::fixy::Result<::fixy::Unit, SwapError> advance(Guarded& guarded, SwapFromState const& from,
                                                                  SwapToState const& to, SwapNanoseconds now) noexcept {
        CRUCIBLE_TRY(_, check_live(guarded, now));
        if (state_.load_acquire() != from.value() || !transition_to(guarded, to.value(), now)) {
            return ::fixy::err(SwapError::InvalidTransition);
        }
        return ::fixy::Unit{};
    }

    // Takes the lock and moves a live swap from `from` to `to`.
    template <class Ctx>
    [[nodiscard]] ::fixy::Result<::fixy::Unit, SwapError> advance_locked(Ctx const& ctx, SwapFromState from,
                                                                         SwapToState to, SwapNanoseconds now) noexcept {
        return guarded_.with(
            ctx, [this, from, to, now](Guarded& guarded) noexcept { return advance(guarded, from, to, now); });
    }

public:
    [[nodiscard]] SwapState state() const noexcept { return state_.load_acquire(); }

    template <class Ctx>
        requires CtxFitsPathSwapRead<Ctx>
    // A copy of the plan.  An Option of a plan is move-only, so the copy
    // comes from a copy of the plan itself.
    [[nodiscard]] ::fixy::Option<PathSwapPlan> plan(Ctx const& ctx) const noexcept {
        return guarded_.with(ctx, [](Guarded const& guarded) noexcept {
            return guarded.plan.match([](PathSwapPlan const& plan) noexcept { return ::fixy::some(plan); },
                                      []() noexcept { return ::fixy::Option<PathSwapPlan>{::fixy::none}; });
        });
    }

    template <class Ctx>
        requires CtxFitsPathSwapRead<Ctx>
    [[nodiscard]] SwapNanoseconds deadline(Ctx const& ctx) const noexcept {
        return guarded_.with(ctx, [](Guarded const& guarded) noexcept { return guarded.deadline; });
    }

    template <class Ctx>
        requires CtxFitsPathSwapRead<Ctx>
    [[nodiscard]] std::uint64_t sequence(Ctx const& ctx) const noexcept {
        return guarded_.with(ctx, [](Guarded const& guarded) noexcept { return guarded.sequence; });
    }

    template <class Ctx>
        requires CtxFitsPathSwapRead<Ctx>
    [[nodiscard]] std::size_t event_count(Ctx const& ctx) const noexcept {
        return guarded_.with(ctx, [](Guarded const& guarded) noexcept { return guarded.event_count; });
    }

    // The ring holds the event with sequence number s at index s - 1,
    // modulo MaxEvents.
    template <class Ctx>
        requires CtxFitsPathSwapRead<Ctx>
    [[nodiscard]] PathSwapEvent event_at(Ctx const& ctx, std::size_t index) const noexcept {
        const EventIndex slot =
            EventIndex::check(index % MaxEvents).expect("a remainder of a division by MaxEvents is below MaxEvents");
        return guarded_.with(ctx, [slot](Guarded const& guarded) noexcept { return guarded.events.at(slot); });
    }

    template <class Ctx>
        requires CtxFitsPathSwapTransition<Ctx>
    [[nodiscard]] ::fixy::Result<::fixy::Unit, SwapError> begin_swap(Ctx const& ctx, DeclaredPathSwapPlan const& plan,
                                                                     SwapNanoseconds now) noexcept {
        PathSwapPlan const& raw = plan.value();
        return guarded_.with(
            ctx, [this, &raw, now](Guarded& guarded) noexcept -> ::fixy::Result<::fixy::Unit, SwapError> {
                const SwapState cur = state_.load_acquire();
                if (cur != SwapState::Stable && cur != SwapState::Complete) {
                    return ::fixy::err(SwapError::InvalidTransition);
                }
                CRUCIBLE_TRY(
                    deadline,
                    ::foundation::core::checked_add(now, raw.timeout().value()).ok_or(SwapError::DeadlineOverflow));
                guarded.plan = ::fixy::some(raw);
                guarded.deadline = deadline;
                // Stable and Complete each lead to Draining, so the transition holds.
                (void)transition_to(guarded, SwapState::Draining, now);
                return ::fixy::Unit{};
            });
    }

    template <class Ctx>
        requires CtxFitsPathSwapTransition<Ctx>
    [[nodiscard]] ::fixy::Result<::fixy::Unit, SwapError> receiver_accepts_bidir(Ctx const& ctx,
                                                                                 SwapNanoseconds now) noexcept {
        return advance_locked(ctx, SwapFromState{SwapState::Draining}, SwapToState{SwapState::BidirReceive}, now);
    }

    template <class Ctx>
        requires CtxFitsPathSwapTransition<Ctx>
    [[nodiscard]] ::fixy::Result<::fixy::Unit, SwapError> sender_observed_drain_ack(Ctx const& ctx,
                                                                                    SwapNanoseconds now) noexcept {
        return advance_locked(ctx, SwapFromState{SwapState::BidirReceive}, SwapToState{SwapState::NewPathFlushing},
                              now);
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
    // would lose them to the detach, so deduction refuses it.  The mint
    // opens the new handle in the context of the swap, so the context
    // admits the effect row of each payload of Proto.
    template <class Ctx, typename Proto, typename OldResource, ::fixy::session::AbandonmentPolicy Policy,
              typename NewResource>
        requires CtxFitsPathSwapTransition<Ctx> && PathSwapSessionResource<NewResource>
              && ::fixy::session::CtxFitsSession<Ctx, Proto, NewResource>
    [[nodiscard, deprecated("CRUCIBLE_STUB: commit_sender migrates no in-flight data. "
                            "The bytes buffered on the old path are dropped.")]]
    auto commit_sender(Ctx const& ctx, ::fixy::session::SessionHandle<Proto, OldResource, void, Policy>&& current,
                       NewResource&& new_resource, SwapNanoseconds now) noexcept
        -> ::fixy::Result<::fixy::session::SessionHandle<Proto, NewResource, void, Policy>,
                          SwapRefused<::fixy::session::SessionHandle<Proto, OldResource, void, Policy>>> {
        // The transition has to land before the detach.  A refused call
        // gives `current` back in the refusal, so the session on the old path
        // goes on, and the audit log gains no event for a transition that
        // never happened.  The detach and the new mint run after the lock is
        // given back.
        auto advanced =
            advance_locked(ctx, SwapFromState{SwapState::NewPathFlushing}, SwapToState{SwapState::Complete}, now);
        if (advanced.is_err()) {
            return ::fixy::err(SwapRefused<::fixy::session::SessionHandle<Proto, OldResource, void, Policy>>{
                std::move(current), std::move(advanced).err().expect("the test above found the error")});
        }
        std::move(current).detach(::fixy::session::detach_reason::TransportClosedOutOfBand{});
        return ::fixy::session::mint_session<Proto, Policy, Ctx, NewResource>(ctx,
                                                                              std::forward<NewResource>(new_resource));
    }

    template <class Ctx>
        requires CtxFitsPathSwapTransition<Ctx>
    [[nodiscard]] ::fixy::Result<::fixy::Unit, SwapError> complete_receiver(Ctx const& ctx,
                                                                            SwapNanoseconds now) noexcept {
        return guarded_.with(ctx, [this, now](Guarded& guarded) noexcept -> ::fixy::Result<::fixy::Unit, SwapError> {
            CRUCIBLE_TRY(_, check_live(guarded, now));
            // No check of the current state comes first.  Complete is not a
            // valid successor of Complete, so the second of two callers is
            // refused.
            if (!transition_to(guarded, SwapState::Complete, now)) {
                return ::fixy::err(SwapError::InvalidTransition);
            }
            return ::fixy::Unit{};
        });
    }
};

template <std::size_t MaxEvents, class Ctx>
    requires CtxFitsPathSwapMint<Ctx>
[[nodiscard]] constexpr PathSwapper<MaxEvents> mint_path_swapper(Ctx const&) noexcept {
    return PathSwapper<MaxEvents>{};
}

}  // namespace crucible::cntp
