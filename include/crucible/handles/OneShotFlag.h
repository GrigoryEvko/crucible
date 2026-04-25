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
};

// Zero-cost: the flag is a single atomic<bool>.
static_assert(sizeof(OneShotFlag) == sizeof(std::atomic<bool>),
              "OneShotFlag must not introduce padding beyond its atomic");

} // namespace crucible::safety
