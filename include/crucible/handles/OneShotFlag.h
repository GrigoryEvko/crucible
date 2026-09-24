#pragma once

#include <crucible/Platform.h>
#include <crucible/safety/Crash.h>

#include <atomic>
#include <concepts>
#include <type_traits>
#include <utility>

namespace crucible::safety {

// The alignment sits on the class rather than on each embedded member,
// so an embedder cannot forget it. One flag then costs a whole cache
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

    // This load is relaxed by design, unlike the one in peek_nothrow. A
    // true result is a hint. Acting on it needs the producer release
    // paired, which this accessor alone does not do.
    [[nodiscard]] CRUCIBLE_INLINE bool peek() const noexcept { return flag_.load(std::memory_order_relaxed); }

    template <typename F>
        requires std::is_invocable_v<F>
    CRUCIBLE_INLINE bool check_and_run(F&& body) noexcept(std::is_nothrow_invocable_v<F>) {
        if (!flag_.load(std::memory_order_relaxed)) [[likely]]
            return false;
        std::atomic_thread_fence(std::memory_order_acquire);
        std::forward<F>(body)();
        flag_.store(false, std::memory_order_release);
        return true;
    }

    // Resetting is correct only where neither side can reach the flag,
    // which the compiler cannot check. The parameter has no runtime
    // role. It exists so every reset site writes that assertion out,
    // and its constructor is explicit so an empty brace does not
    // satisfy it.
    struct QuiescenceProof {
        explicit QuiescenceProof() = default;
    };

    void reset_in_quiescent_context(QuiescenceProof) noexcept { flag_.store(false, std::memory_order_relaxed); }

    struct signal_marker {};

    // This load is acquire where peek is relaxed. It pairs with the
    // release in signal, so a true result is safe to act on with no
    // fence at the call site. The divergence from peek is deliberate.
    [[nodiscard]] CRUCIBLE_INLINE Crash<CrashClass_v::NoThrow, bool> peek_nothrow() const noexcept {
        return Crash<CrashClass_v::NoThrow, bool>{flag_.load(std::memory_order_acquire)};
    }

    [[nodiscard]] CRUCIBLE_INLINE Crash<CrashClass_v::Throw, signal_marker> signal_throw() noexcept {
        flag_.store(true, std::memory_order_release);
        return Crash<CrashClass_v::Throw, signal_marker>{signal_marker{}};
    }

    template <typename F>
        requires std::is_invocable_v<F>
    [[nodiscard]] CRUCIBLE_INLINE Crash<CrashClass_v::ErrorReturn, bool>
    try_acknowledge_error_return(F&& body) noexcept(std::is_nothrow_invocable_v<F>) {
        return Crash<CrashClass_v::ErrorReturn, bool>{check_and_run(std::forward<F>(body))};
    }
};

static_assert(alignof(OneShotFlag) >= 64, "OneShotFlag must be cache-line aligned to prevent false "
                                          "sharing on the cross-thread signal path.");
static_assert(sizeof(OneShotFlag) >= 64, "OneShotFlag occupies a full cache line by construction; "
                                         "embedders rely on the flag NOT sharing a line with any "
                                         "field touched on the consumer's hot path.");

}  // namespace crucible::safety
