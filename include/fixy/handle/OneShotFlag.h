#pragma once

// A one-shot cross-thread signal.  A producer raises the flag, and a
// consumer runs a body once for each time it finds the flag raised.
//
// Design notes:
//
//  1. check_and_run takes the flag down with one exchange before the
//     body runs, so no signal is lost.  A signal that arrives during the
//     body raises the flag again, and the next check runs the body again.
//
//  2. The exchange gives each raise to one consumer.  Two consumers
//     cannot both run the body for one signal, so several consumers need
//     no rule that the type cannot state.

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
