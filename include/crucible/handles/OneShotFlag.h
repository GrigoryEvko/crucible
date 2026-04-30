#pragma once

// ── crucible::safety::OneShotFlag ───────────────────────────────────
//
// Cross-thread "signal, then ack" primitive.  The producer thread sets
// the flag (release-ordered) alongside whatever state it is asking the
// consumer to observe.  The consumer thread tests the flag with a
// relaxed load on the hot path, and — on the rare true case — takes
// an acquire fence, processes the signal, and clears the flag with a
// release store so subsequent checks see the post-ack state.
//
// The idiom is written out by hand at every consumer site today:
//
//   if (flag.load(std::memory_order_relaxed)) [[unlikely]] {
//     std::atomic_thread_fence(std::memory_order_acquire);
//     ...body...
//     flag.store(false, std::memory_order_release);
//   }
//
// OneShotFlag fuses those four operations into `signal()` + a single
// `check_and_run(body)` call so that consumers cannot accidentally
// omit the fence, reorder the clear, or use the wrong memory order.
//
//   Axiom coverage: ThreadSafe (the ordering is load-bearing).
//   Runtime cost:   hot path is a single relaxed atomic load + branch;
//                   the acquire fence and release store run only on
//                   the signalled path (compile-time-bounded via
//                   [[likely]]).

#include <crucible/Platform.h>
#include <crucible/safety/Crash.h>

#include <atomic>
#include <concepts>
#include <type_traits>
#include <utility>

namespace crucible::safety {

class OneShotFlag {
    std::atomic<bool> flag_{false};

public:
    OneShotFlag() = default;

    OneShotFlag(const OneShotFlag&)            = delete("OneShotFlag is an inter-thread signal; copy would split ownership");
    OneShotFlag& operator=(const OneShotFlag&) = delete("OneShotFlag is an inter-thread signal; copy would split ownership");
    OneShotFlag(OneShotFlag&&)                 = delete("OneShotFlag is an inter-thread signal; move breaks acquire/release");
    OneShotFlag& operator=(OneShotFlag&&)      = delete("OneShotFlag is an inter-thread signal; move breaks acquire/release");

    // ── Producer side ──────────────────────────────────────────────

    // Signal the flag.  Release-ordered so state writes the producer
    // made before this call are visible to any consumer that observes
    // the flag set (via the acquire fence inside check_and_run).
    CRUCIBLE_INLINE void signal() noexcept {
        flag_.store(true, std::memory_order_release);
    }

    // ── Consumer side ──────────────────────────────────────────────

    // Relaxed peek — diagnostic only.  Callers that want to act on
    // the observed signal must use check_and_run so the acquire fence
    // is paired with the producer's release.
    [[nodiscard]] CRUCIBLE_INLINE bool peek() const noexcept {
        return flag_.load(std::memory_order_relaxed);
    }

    // Consume the signal if set: relaxed-test, acquire-fence if set,
    // run body, release-clear.  Returns true iff body was invoked.
    // The body is called at most once per signal — clearing is the
    // acknowledgement, and is always performed after the body runs
    // (release-ordered so subsequent peek()s see the clear).
    template <typename F>
        requires std::is_invocable_v<F>
    CRUCIBLE_INLINE bool check_and_run(F&& body)
        noexcept(std::is_nothrow_invocable_v<F>)
    {
        if (!flag_.load(std::memory_order_relaxed)) [[likely]] return false;
        // Pair with signal()'s release store — any state the producer
        // wrote before signal() is now visible.
        std::atomic_thread_fence(std::memory_order_acquire);
        std::forward<F>(body)();
        // Release so subsequent observers see cleared flag + any state
        // the consumer mutated during body.
        flag_.store(false, std::memory_order_release);
        return true;
    }

    // Unconditional reset — only safe when both producer and consumer
    // are quiescent (e.g. shutdown / reset-all paths).  Relaxed store
    // matches TraceRing::reset's assumption that no concurrent access
    // exists at the call site.
    void reset_unsafe() noexcept {
        flag_.store(false, std::memory_order_relaxed);
    }

    // ═══════════════════════════════════════════════════════════════
    // FOUND-G62: Crash-pinned production surface
    // ═══════════════════════════════════════════════════════════════
    //
    // The OneShotFlag-guarded boundary discipline (28_04 §4.3.10)
    // assigns a CrashClass to each call site:
    //
    //   peek_nothrow() → Crash<NoThrow, bool>   — STEADY-STATE consumer.
    //                    The peek itself is a single relaxed atomic
    //                    load + branch; it never fails, never throws,
    //                    never blocks, never aborts.  The recovery
    //                    branch (when peek returns true) is handled
    //                    separately at a wider call boundary.  The
    //                    NoThrow class is the strongest in the
    //                    CrashLattice, so this value satisfies any
    //                    consumer-side gate (NoThrow / ErrorReturn /
    //                    Throw / Abort).
    //
    //   signal_throw() → Crash<Throw, void>     — PRODUCER side.
    //                    The signal action raises the crash signal;
    //                    pinning Throw class declares "this call may
    //                    initiate a peer-failure transition".  The
    //                    return type is `void` (no value), so the
    //                    wrapper tags an empty marker — used by
    //                    diagnostic call-site discipline only.
    //                    void requires special handling — we wrap
    //                    a tag struct.  See `signal_marker` below.
    //
    //   try_acknowledge_error_return(F) → Crash<ErrorReturn, bool>
    //                    — RECOVERY-HANDLER variant of check_and_run.
    //                    Pins ErrorReturn class because the body F is
    //                    invoked only on the unlikely true path; that
    //                    body is the recovery action, which by the
    //                    Crash discipline returns errors via
    //                    std::expected and is classified ErrorReturn.
    //                    Returns true iff body was invoked.
    //
    // Why additive (not replacing): peek/signal/check_and_run are
    // consumed by ~10 production call sites in CrashTransport.h and
    // related crash-watched handles.  An additive overlay preserves
    // those without churn while letting NEW call sites declare their
    // crash-classification at the type level.

    // Empty tag returned by signal_throw — encodes "the signal call
    // happened" without carrying a value.  Allows wrapping in
    // Crash<Throw, signal_marker> for type-level call-site auditing.
    struct signal_marker { };

    // Steady-state consumer pin — peek returns Crash<NoThrow, bool>.
    [[nodiscard]] CRUCIBLE_INLINE
    Crash<CrashClass_v::NoThrow, bool>
    peek_nothrow() const noexcept {
        return Crash<CrashClass_v::NoThrow, bool>{
            flag_.load(std::memory_order_relaxed)};
    }

    // Producer-side signal — pins the action class as Throw.  The
    // returned marker is consumed (move-only) by the type system so
    // a refactor that drops the discriminator surfaces as an unused-
    // result diagnostic on the [[nodiscard]] Crash type.
    [[nodiscard]] CRUCIBLE_INLINE
    Crash<CrashClass_v::Throw, signal_marker>
    signal_throw() noexcept {
        flag_.store(true, std::memory_order_release);
        return Crash<CrashClass_v::Throw, signal_marker>{signal_marker{}};
    }

    // Recovery-handler classification — body F runs only on the
    // unlikely true path, so the call is pinned ErrorReturn.
    template <typename F>
        requires std::is_invocable_v<F>
    [[nodiscard]] CRUCIBLE_INLINE
    Crash<CrashClass_v::ErrorReturn, bool>
    try_acknowledge_error_return(F&& body)
        noexcept(std::is_nothrow_invocable_v<F>)
    {
        return Crash<CrashClass_v::ErrorReturn, bool>{
            check_and_run(std::forward<F>(body))};
    }
};

// Zero-cost: the flag is a single atomic<bool>.
static_assert(sizeof(OneShotFlag) == sizeof(std::atomic<bool>),
              "OneShotFlag must not introduce padding beyond its atomic");

} // namespace crucible::safety
