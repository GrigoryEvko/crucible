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
//
//  3. A reset takes the proof that no thread of the owner of the flag runs
//     (the quiescence of an owner of fixy/os/OwnedThread.h).

#include <fixy/os/OwnedThread.h>
#include <foundation/Platform.h>
#include <foundation/core/Atomic.h>
#include <foundation/core/Fatal.h>

#include <concepts>
#include <type_traits>
#include <utility>

namespace fixy::handle {

// The alignment sits on the class rather than on each embedded member,
// so an embedder cannot forget it.  One flag then costs a whole cache
// line instead of one byte, which is the price of the guarantee.
//
// The flag is an Atomic of the base, and each door reads or writes it
// atomically.  So two threads can use one flag at the same time: the class
// is Synchronized by its structure, and it states no transfer fact
// (foundation/Transfer.h).
class alignas(64) OneShotFlag {
    ::foundation::core::Atomic<bool> flag_{false};

public:
    OneShotFlag() = default;

    OneShotFlag(const OneShotFlag&) = delete("OneShotFlag is an inter-thread signal; copy would split ownership");
    OneShotFlag&
    operator=(const OneShotFlag&) = delete("OneShotFlag is an inter-thread signal; copy would split ownership");
    OneShotFlag(OneShotFlag&&) = delete("OneShotFlag is an inter-thread signal; move breaks acquire/release");
    OneShotFlag&
    operator=(OneShotFlag&&) = delete("OneShotFlag is an inter-thread signal; move breaks acquire/release");

    CRUCIBLE_INLINE void signal() noexcept { flag_.store_release(true); }

    // A consumer reads a flag that the producer stores, so the load takes
    // acquire order.  On x86_64 it is the same plain load as a relaxed load.
    [[nodiscard]] CRUCIBLE_INLINE bool peek() const noexcept { return flag_.load_acquire(); }

    // The first load keeps the path that finds no signal to one load and
    // one branch.  It reads a flag of the producer, so it takes acquire
    // order.  The exchange takes the flag down before the body runs, and its
    // acquire half pairs with the release in signal, so the body sees every
    // write the producer made before its signal.  Two signals that arrive
    // before one check run the body once.
    template <typename F>
        requires std::is_invocable_v<F>
    [[nodiscard]] CRUCIBLE_INLINE bool check_and_run(F&& body) noexcept(std::is_nothrow_invocable_v<F>) {
        if (!flag_.load_acquire()) [[likely]]
            return false;
        if (!flag_.exchange_acq_rel(false)) return false;
        std::forward<F>(body)();
        return true;
    }

    // A reset is correct only where neither side can reach the flag.  The
    // proof shows that no thread of the owner of the flag runs (the
    // quiescence of an owner), so the caller is the one thread that reaches
    // the flag.  The store is atomic, so a reset races with no read in any
    // case.  A flag that does not lie inside the owner of the proof ends the
    // process, and so does a proof whose OwnerThread holds a thread again.
    template <class Owner>
    void reset_in_quiescent_context(::fixy::thread::QuiescenceProof<Owner> const& proof) noexcept {
        if (!proof.holds_part(*this)) [[unlikely]] {
            refuse_foreign_reset_();
        }
        if (!proof.is_quiescent()) [[unlikely]] {
            refuse_reset_while_the_thread_runs_();
        }
        flag_.store_release(false);
    }

private:
    [[noreturn]] CRUCIBLE_COLD static void refuse_foreign_reset_() noexcept {
        ::foundation::core::fatal(
            "OneShotFlag: a reset took the proof of quiescence of an owner that does not hold the "
            "flag.  Take the proof from the OwnerThread of the owner of the flag");
    }

    [[noreturn]] CRUCIBLE_COLD static void refuse_reset_while_the_thread_runs_() noexcept {
        ::foundation::core::fatal(
            "OneShotFlag: a reset took a proof of quiescence while the thread of the owner runs again.  Take a "
            "new proof after stop_and_join");
    }
};

}  // namespace fixy::handle
