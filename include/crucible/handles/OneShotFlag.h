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
//
// ── Cache-line isolation (fixy-A1-001) ──────────────────────────────
//
// `alignas(64)` at the class level pins every OneShotFlag instance to
// its own cache line.  The producer side (`signal()` / `signal_throw()`)
// release-stores `flag_`; the consumer side splits by intent — `peek()`
// is the relaxed-by-design hot-path primitive (callers manually pair
// with an acquire fence inside their `[[unlikely]]` branch before
// acting; see bridges/CrashTransport.h for the five-site discipline);
// `peek_nothrow()` is the safe-by-default acquire-load FOUND-G62
// Crash<NoThrow,bool> surface that newer consumers can use directly
// without a manual fence (fixy-A1-006).  `check_and_run()` couples a
// relaxed test with an acquire fence on the true path and a release-
// clear, so it is the only synchronizing consume-and-clear primitive.
// Cross-
// thread MESI traffic must NOT ping-pong against unrelated state in the
// embedding struct — otherwise every producer write to an adjacent
// (logically unrelated) field invalidates the consumer's cache line and
// the relaxed-load that should cost ~1 cycle costs an L3 round-trip
// instead (~35-50 cycles on the dev hardware, more cross-socket).
//
// The discipline used to live at the embed site
// (BackgroundThread.h:169/215 apply `alignas(64)` to `stop_requested`
// and `reset_requested`).  Per CLAUDE.md §IX cross-thread-atomic rule,
// every cross-thread atomic deserves its own line — so we hoist the
// alignment INTO the class itself.  Every embedder is now cache-line-
// isolated by construction; existing per-embed `alignas(64)` directives
// remain valid (a stricter alignment is always honoured) but they are
// now defensive depth, not the load-bearing rule.
//
// Cost: 64 bytes per OneShotFlag (vs 1 for the bare atomic<bool>).
// Crucible deployments hold O(threads) flags — a handful per pipeline,
// tens per fleet — so the absolute memory cost is rounding error and
// the latency-cost-of-false-sharing saved at every relaxed-load is the
// dominant signal.

#include <crucible/Platform.h>
#include <crucible/safety/Crash.h>

#include <atomic>
#include <concepts>
#include <type_traits>
#include <utility>

namespace crucible::safety {

class alignas(64) OneShotFlag {
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

    // ── reset_in_quiescent_context — quiescence-gated reset ────────
    //
    // Unconditional reset — only safe when both producer and consumer
    // are quiescent (e.g. shutdown / reset-all paths).  Relaxed store
    // matches TraceRing::reset's assumption that no concurrent access
    // exists at the call site.
    //
    // fixy-A1-032 (task #1574): replaces the prior `reset_unsafe()`
    // surface.  Two improvements:
    //
    //   1. Renamed to `reset_in_quiescent_context` so the precondition
    //      is grep-discoverable from the call site name — readers and
    //      code-review agents do not need to chase the `_unsafe` suffix
    //      back to this header to learn what the unsafety is.
    //
    //   2. Gated behind the `QuiescenceProof` passkey (below).  The
    //      passkey's default ctor is `explicit`, so:
    //          flag.reset_in_quiescent_context()        — compile error
    //          flag.reset_in_quiescent_context({})      — compile error
    //          flag.reset_in_quiescent_context(QuiescenceProof{}) — OK
    //      Every certified-quiescent reset site spells out the proof
    //      construction at the call site, which audits via `grep
    //      "QuiescenceProof{}"`.  The runtime cost of the parameter
    //      is zero (sizeof(QuiescenceProof) == 1 byte; EBO-collapsed
    //      against the function parameter ABI on every supported
    //      target).
    //
    // The discipline is "every reset call site explicitly asserts the
    // precondition", not "only friends may reset" — call sites in
    // BackgroundThread and tests are the audit ground truth.  A future
    // hardening could close the passkey gate (private default ctor +
    // friend list); the current shape is the minimum that makes the
    // precondition visible without friend-list churn.
    struct QuiescenceProof {
        // Default ctor is `explicit` — callers must spell out
        // `QuiescenceProof{}` (or a named local) at every call site.
        // Implicit braced-init (`reset_in_quiescent_context({})`)
        // fails: §II TypeSafe demands the precondition be visible.
        explicit QuiescenceProof() = default;
    };

    void reset_in_quiescent_context(QuiescenceProof) noexcept {
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
    //                    A single acquire atomic load + branch; it
    //                    never fails, never throws, never blocks,
    //                    never aborts.  The acquire order pairs with
    //                    the producer's release in signal() / signal_
    //                    throw(), so any state the producer mutated
    //                    before signal is visible to a consumer that
    //                    observed a `true` return WITHOUT requiring an
    //                    explicit atomic_thread_fence at the call site
    //                    (safe-by-default; ThreadSafe axiom per
    //                    CLAUDE.md §II.6, fixy-A1-006 / task #1548).
    //                    This is the SEMANTIC DIVERGENCE from the
    //                    lower-level peek() primitive, which stays
    //                    relaxed-by-design as a hot-path fast-path
    //                    test that callers manually pair with check_
    //                    and_run (or an inline acquire fence) before
    //                    acting on the observed signal — five sites in
    //                    bridges/CrashTransport.h follow that pattern.
    //                    peek_nothrow is the FOUND-G62 type-audited
    //                    surface and gets the stronger order so future
    //                    consumers can rely on the value directly.
    //                    The NoThrow class is the strongest in the
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
    // Acquire order (NOT relaxed): pairs with signal()'s release-store
    // so a `true` return is safe-to-act-on without an explicit fence
    // at the call site.  Diverges from the lower-level peek() primitive
    // by design — see the FOUND-G62 doc-block above and the audit in
    // fixy-A1-006 (#1548) / CLAUDE.md §II.6 ThreadSafe.
    [[nodiscard]] CRUCIBLE_INLINE
    Crash<CrashClass_v::NoThrow, bool>
    peek_nothrow() const noexcept {
        return Crash<CrashClass_v::NoThrow, bool>{
            flag_.load(std::memory_order_acquire)};
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

// Cache-line isolation: every OneShotFlag occupies a full cache line
// so cross-thread signal stores never invalidate adjacent state.
// alignof claim is the structural guarantee; sizeof follows from the
// standard rule that sizeof is a multiple of alignof.
static_assert(alignof(OneShotFlag) >= 64,
              "OneShotFlag must be cache-line aligned to prevent false "
              "sharing on the cross-thread signal path (CLAUDE.md §IX).");
static_assert(sizeof(OneShotFlag) >= 64,
              "OneShotFlag occupies a full cache line by construction; "
              "embedders rely on the flag NOT sharing a line with any "
              "field touched on the consumer's hot path.");

} // namespace crucible::safety
