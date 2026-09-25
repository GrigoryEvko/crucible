#pragma once

// A one-shot cross-thread signal.  A producer raises the flag, and a
// consumer runs a body once for each time it finds the flag raised.
//
// Old spelling: include/crucible/handles/OneShotFlag.h.
//
// Deviations, each deliberate:
//
//  1. The crash classes folded into the fixy ctrl atoms, and nothing in
//     this tree throws.  peek_nothrow was an acquire load typed
//     Crash<NoThrow, bool>, and NoThrow is the strict default, so it is
//     peek_acquire and returns bool.  signal_throw raised the flag and
//     returned Crash<Throw, signal_marker>, a class nothing can have
//     here, so it and signal_marker are gone: call signal.
//     try_acknowledge_error_return returned Crash<ErrorReturn, bool> over
//     check_and_run, and check_and_run is now [[nodiscard]], so its bool
//     return is the error return.
//
//  2. check_and_run lost a signal.  It ran the body and then stored
//     false, so a signal that arrived while the body ran was erased.  It
//     now takes the flag down with one exchange before the body runs.  A
//     signal that arrives during the body raises the flag again, and the
//     next check runs the body again.
//
//  3. The old check was correct only for one consumer.  Two consumers
//     could both read the flag raised and both run the body for one
//     signal.  The exchange gives each raise to one consumer, so several
//     consumers no longer need a rule the type cannot state.

#include <foundation/Platform.h>

#include <atomic>
#include <concepts>
#include <type_traits>
#include <utility>

namespace fixy::handle {

// The alignment sits on the class rather than on each embedded member,
// so an embedder cannot forget it.  One flag then costs a whole cache
// line instead of one byte, which is the price of the guarantee.
class alignas(64) OneShotFlag {
    std::atomic<bool> flag_{false};

public:
    OneShotFlag() = default;

    OneShotFlag(const OneShotFlag&) = delete("OneShotFlag is an inter-thread signal; copy would split ownership");
    OneShotFlag&
    operator=(const OneShotFlag&) = delete("OneShotFlag is an inter-thread signal; copy would split ownership");
    OneShotFlag(OneShotFlag&&) = delete("OneShotFlag is an inter-thread signal; move breaks acquire/release");
    OneShotFlag&
    operator=(OneShotFlag&&) = delete("OneShotFlag is an inter-thread signal; move breaks acquire/release");

    CRUCIBLE_INLINE void signal() noexcept { flag_.store(true, std::memory_order_release); }

    // This load is relaxed by design, unlike the one in peek_acquire.  A
    // true result is a hint.  Acting on it needs the producer release
    // paired, which this accessor alone does not do.
    [[nodiscard]] CRUCIBLE_INLINE bool peek() const noexcept { return flag_.load(std::memory_order_relaxed); }

    // The relaxed load keeps the path that finds no signal to one load and
    // one branch.  The exchange takes the flag down before the body runs,
    // and its acquire half pairs with the release in signal, so the body
    // sees every write the producer made before its signal.  Two signals
    // that arrive before one check run the body once.
    template <typename F>
        requires std::is_invocable_v<F>
    [[nodiscard]] CRUCIBLE_INLINE bool check_and_run(F&& body) noexcept(std::is_nothrow_invocable_v<F>) {
        if (!flag_.load(std::memory_order_relaxed)) [[likely]]
            return false;
        if (!flag_.exchange(false, std::memory_order_acq_rel)) return false;
        std::forward<F>(body)();
        return true;
    }

    // Resetting is correct only where neither side can reach the flag,
    // which the compiler cannot check.  The parameter has no runtime
    // role.  It exists so every reset site writes that assertion out,
    // and its constructor is explicit so an empty brace does not
    // satisfy it.
    struct QuiescenceProof {
        explicit QuiescenceProof() = default;
    };

    void reset_in_quiescent_context(QuiescenceProof) noexcept { flag_.store(false, std::memory_order_relaxed); }

    // This load is acquire where peek is relaxed.  It pairs with the
    // release in signal, so a true result is safe to act on with no
    // fence at the call site.  The divergence from peek is deliberate.
    [[nodiscard]] CRUCIBLE_INLINE bool peek_acquire() const noexcept { return flag_.load(std::memory_order_acquire); }
};

static_assert(alignof(OneShotFlag) >= 64, "OneShotFlag must be cache-line aligned to prevent false "
                                          "sharing on the cross-thread signal path.");
static_assert(sizeof(OneShotFlag) >= 64, "OneShotFlag occupies a full cache line by construction; "
                                         "embedders rely on the flag NOT sharing a line with any "
                                         "field touched on the consumer's hot path.");

}  // namespace fixy::handle
