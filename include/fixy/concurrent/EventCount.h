#pragma once

// An event count: a wait on a condition that costs the notifier one fence and
// one load when no thread waits, and that sleeps in the kernel after a spin
// budget (the event count of Vyukov, as Eigen and folly give it).
//
// Protocol
//   A waiter takes four steps:
//     1. prepare_wait() enters the waiter in the count of the waiters, reads
//        the epoch, and runs fence_seq_cst.  It gives the entry of the waiter
//        with the epoch.
//     2. The waiter tests its condition again.
//     3. When the condition holds, the waiter ends the scope of the entry,
//        and the entry leaves the count.
//     4. Otherwise commit_wait(ctx, entry, deadline) sleeps while the epoch
//        holds the epoch of the entry.  The entry then leaves the count at
//        the end of its scope.
//   A notifier stores its condition first.  Then notify_one(ctx) or
//   notify_all(ctx) runs fence_seq_cst and loads the count.  A count of zero
//   ends the call.  Otherwise the notifier adds one to the epoch and wakes the
//   waiters of the epoch word.
//
// Order
//   The fence of the waiter orders its entry in the count, a store, before
//   its test of the condition, a load.  The fence of the notifier orders its
//   store of the condition before its load of the count.  The fences give
//   this store-load order on each side, so at least one of the two loads reads
//   the store of the other thread.  Thus the waiter sees the condition, or the
//   notifier sees the waiter.  A notifier that sees the waiter adds one to the
//   epoch, and then it wakes.  The kernel reads the epoch word in the lock of
//   its queue of waiters.  So a waiter that read the old epoch finds the new
//   epoch and does not sleep, or it sleeps before the wake and the wake ends
//   its sleep.  No wake is lost.
//
//   A waiter that reads an epoch after the add of a notifier reads it with
//   acquire order, and the add has release order.  So the waiter also sees
//   the condition that the notifier stored before its add.
//
// Epoch
//   The epoch is a word of 32 bits.  2^32 notifies that each find a waiter,
//   between the read of the epoch by a waiter and its sleep, bring the word
//   back to the value of the waiter.  The waiter then sleeps until the next
//   notify.  Each such notify makes a system call, so 2^32 of them take more
//   than an hour, and a waiter goes from its read to its sleep in some
//   microseconds.
//
// Entry
//   prepare_wait gives an EventWait, the entry of one waiter in the count.
//   The entry cannot copy or move, and its destructor leaves the count.  So
//   each path of a waiter leaves the count one time, and the end of the scope
//   of the entry cancels a wait.  A waiter that stays in the count only makes
//   each notify do a system call for nothing.
//
// Gate
//   commit_wait sleeps, so it takes a context that owns Block.  A notify that
//   finds a waiter makes a system call, so it takes such a context too.  The
//   foreground context owns no Block, so the type keeps the futex off the hot
//   path, as CLAUDE.md asks.
//
// Policy
//   A wait policy names how a consumer waits.  Spin spins with the plain arm
//   of the process and never parks, so each context takes it.
//   SpinThenPark<BudgetNanos> spins for its budget with the spin budget of
//   fixy/os/SpinBudget.h, and then it parks on an event count.  The spin
//   reads the counter of the core and the park blocks, so the policy needs a
//   context that owns Nondet and Block.  A context of the hot path owns
//   neither, so it cannot take the policy.

#include <fixy/os/Futex.h>
#include <fixy/os/SpinBudget.h>
#include <foundation/Pinned.h>
#include <foundation/Platform.h>
#include <foundation/core/Atomic.h>
#include <foundation/core/Choice.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>
#include <foundation/os/SpinHint.h>

#include <cstdint>
#include <type_traits>
#include <utility>

namespace fixy::concurrent {

// How a wait of an event count ended.
enum class EventWaitEnd : std::uint8_t {
    // A notifier changed the epoch, or the condition of the waiter held.
    notified,
    // The deadline passed, and the epoch and the condition did not change.
    deadline_passed,
};

// How many waiters a wake of an event count ends.
enum class WakeCount : std::uint8_t {
    one,
    all,
};

class EventCount;

// The entry of one waiter in the count of the waiters (the entry).
class [[nodiscard]] EventWait {
public:
    EventWait(EventWait const&) = delete("an entry in the count of the waiters belongs to one wait");
    EventWait(EventWait&&) = delete("an entry in the count of the waiters belongs to one wait");
    EventWait& operator=(EventWait const&) = delete("an entry in the count of the waiters belongs to one wait");
    EventWait& operator=(EventWait&&) = delete("an entry in the count of the waiters belongs to one wait");
    inline ~EventWait();

private:
    friend class EventCount;

    EventWait(EventCount& events, std::uint32_t epoch) noexcept;

    // The count of the entry.  The structure that holds the event count owns
    // it, such as the parks of a pipeline, and the count is Pinned, so it does
    // not move.  The one constructor sets the member from a reference, so it
    // is never null, and the const keeps it set: the entry has no setter.  Only
    // prepare_wait makes an entry, and an entry does not copy or move.  So the
    // entry lives in the scope of one wait, which reaches the count by
    // reference, and the count outlives the entry.
    EventCount* const events_ = nullptr;
    std::uint32_t epoch_ = 0;
};

// The event count (the protocol).  The two words share one cache line, and no
// other data shares it.  A notifier with no waiter only reads the line, so the
// line stays shared between the cores of the notifier and of the waiter.
class alignas(64) EventCount : ::foundation::Pinned<EventCount> {
public:
    constexpr EventCount() noexcept = default;

    // Step 1 of a waiter: the entry in the count, with the epoch.
    [[nodiscard]] EventWait prepare_wait() noexcept {
        static_cast<void>(waiters_.fetch_add_acq_rel(1));
        std::uint32_t const epoch = epoch_.load_acquire();
        ::foundation::core::fence_seq_cst();
        return EventWait{*this, epoch};
    }

    // Step 4 of a waiter: sleeps while the epoch holds the epoch of ENTRY, or
    // until DEADLINE.  A signal handler and a wake with no cause end one sleep,
    // and the wait sleeps again with the same deadline.  ENTRY is an entry of
    // this count.
    template <class Ctx>
        requires ::fixy::futex::CtxMayUseFutex<Ctx>
    [[nodiscard]] EventWaitEnd commit_wait(Ctx const& ctx, EventWait const& entry,
                                           ::foundation::core::Option<::fixy::futex::Deadline> deadline) noexcept {
        CRUCIBLE_FATAL_INVARIANT(entry.events_ == this);
        while (epoch_.load_acquire() == entry.epoch_) {
            ::fixy::futex::WaitEnd const end =
                ::fixy::futex::wait(ctx, epoch_, entry.epoch_, deadline)
                    .expect("the kernel refused the futex wait of an event count on its own word");
            if (end == ::fixy::futex::WaitEnd::deadline_passed) {
                return epoch_.load_acquire() == entry.epoch_ ? EventWaitEnd::deadline_passed : EventWaitEnd::notified;
            }
        }
        return EventWaitEnd::notified;
    }

    // Wakes at most one waiter, after the store of the condition of the
    // caller.  With no waiter, the call is one fence and one load.
    template <class Ctx>
        requires ::fixy::futex::CtxMayUseFutex<Ctx>
    void notify_one(Ctx const& ctx) noexcept {
        ::foundation::core::fence_seq_cst();
        if (waiters_.load_acquire() == 0) [[likely]] {
            return;
        }
        wake_(ctx, WakeCount::one);
    }

    // Wakes each waiter, after the store of the condition of the caller.
    // With no waiter, the call is one fence and one load.
    template <class Ctx>
        requires ::fixy::futex::CtxMayUseFutex<Ctx>
    void notify_all(Ctx const& ctx) noexcept {
        ::foundation::core::fence_seq_cst();
        if (waiters_.load_acquire() == 0) [[likely]] {
            return;
        }
        wake_(ctx, WakeCount::all);
    }

    // True when a waiter is in the count.  The answer changes with the
    // threads, so the read takes the bare tag of Nondet.
    [[nodiscard]] bool has_waiters(::foundation::effects::cap::Nondet /*nondet*/) const noexcept {
        return waiters_.load_acquire() != 0;
    }

private:
    friend class EventWait;

    template <class Ctx>
    CRUCIBLE_COLD void wake_(Ctx const& ctx, WakeCount count) noexcept {
        static_cast<void>(epoch_.fetch_add_acq_rel(1));
        if (count == WakeCount::all) {
            static_cast<void>(::fixy::futex::wake_all(ctx, epoch_)
                                  .expect("the kernel refused the futex wake of an event count on its own word"));
        } else {
            static_cast<void>(::fixy::futex::wake_one(ctx, epoch_)
                                  .expect("the kernel refused the futex wake of an event count on its own word"));
        }
    }

    void leave_() noexcept { static_cast<void>(waiters_.fetch_sub_acq_rel(1)); }

    ::foundation::core::Atomic<std::uint32_t> waiters_{0};
    ::fixy::futex::FutexWord epoch_{0};
};

inline EventWait::EventWait(EventCount& events, std::uint32_t epoch) noexcept
    : events_{__builtin_addressof(events)}, epoch_{epoch} {}

inline EventWait::~EventWait() { events_->leave_(); }

// The wait policy that spins and never parks (the policy).
struct Spin final {
    static constexpr bool should_park = false;
};

// The wait policy that spins for BudgetNanos, and then parks (the policy).
template <std::uint32_t BudgetNanos>
struct SpinThenPark final {
    static constexpr bool should_park = true;
    static constexpr ::fixy::time::SpinBudget budget{BudgetNanos};
};

namespace detail {

template <class Policy>
inline constexpr bool is_wait_policy = std::is_same_v<Policy, Spin>;

template <std::uint32_t BudgetNanos>
inline constexpr bool is_wait_policy<SpinThenPark<BudgetNanos>> = true;

}  // namespace detail

// Spin, or SpinThenPark with a budget.
template <class Policy>
concept WaitPolicy = detail::is_wait_policy<Policy>;

// A context that may wait by POLICY: each context for Spin, and a context
// that owns Nondet and Block for a policy that parks.
template <class Ctx, class Policy>
concept CtxFitsWaitPolicy = ::foundation::effects::IsExecCtx<Ctx> && WaitPolicy<Policy>
                         && (!Policy::should_park
                             || ::foundation::effects::CtxOwnsAllOf<Ctx, ::foundation::effects::Effect::Block,
                                                                    ::foundation::effects::Effect::Nondet>);

// A condition of a wait: a call with no argument that gives a bool and does
// not throw.
template <class Condition>
concept WaitCondition = std::is_nothrow_invocable_r_v<bool, Condition&>;

// Waits by POLICY until CONDITION holds.  Spin spins with the plain arm of
// the process.  SpinThenPark spins for its budget, and then it parks on
// EVENTS, whose notifier stores the condition before each notify.
template <class Policy, class Ctx, class Condition>
    requires CtxFitsWaitPolicy<Ctx, Policy> && WaitCondition<Condition>
void wait_for(Ctx const& ctx, EventCount& events, Condition&& condition) noexcept {
    if constexpr (!Policy::should_park) {
        static_cast<void>(ctx);
        static_cast<void>(events);
        ::foundation::os::SpinArm const arm = ::foundation::os::spin_choice_of_process().plain;
        while (!condition()) {
            ::foundation::os::spin_turn(arm);
        }
    } else {
        ::foundation::effects::cap::Nondet const nondet =
            ::foundation::effects::extract_bare_from_ctx<::foundation::effects::Effect::Nondet>(ctx);
        if (::fixy::time::spin_until(nondet, ::fixy::time::SpinDeadline::after(nondet, Policy::budget), condition)) {
            return;
        }
        for (;;) {
            EventWait const entry = events.prepare_wait();
            if (condition()) return;
            static_cast<void>(events.commit_wait(ctx, entry, ::foundation::core::none));
        }
    }
}

// Waits by POLICY until CONDITION holds or DEADLINE passes.  A deadline needs
// the park, so POLICY parks.  The result is notified when the condition
// holds.
template <class Policy, class Ctx, class Condition>
    requires CtxFitsWaitPolicy<Ctx, Policy> && WaitCondition<Condition> && (Policy::should_park)
[[nodiscard]] EventWaitEnd wait_for(Ctx const& ctx, EventCount& events, Condition&& condition,
                                    ::fixy::futex::Deadline const& deadline) noexcept {
    ::foundation::effects::cap::Nondet const nondet =
        ::foundation::effects::extract_bare_from_ctx<::foundation::effects::Effect::Nondet>(ctx);
    if (::fixy::time::spin_until(nondet, ::fixy::time::SpinDeadline::after(nondet, Policy::budget), condition)) {
        return EventWaitEnd::notified;
    }
    for (;;) {
        EventWait const entry = events.prepare_wait();
        if (condition()) return EventWaitEnd::notified;
        if (events.commit_wait(ctx, entry, ::foundation::core::Option<::fixy::futex::Deadline>::some(deadline))
            == EventWaitEnd::deadline_passed) {
            return condition() ? EventWaitEnd::notified : EventWaitEnd::deadline_passed;
        }
    }
}

}  // namespace fixy::concurrent
