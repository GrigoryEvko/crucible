#pragma once

// The claim of one thread on one object, with no brand and no registry.
//
// ThreadClaimCell holds the thread that holds the cell, or no thread.  A
// thread takes a free cell, and only the holder gives it up.  No step ends the
// process: a step that the caller cannot take gives false, and it changes
// nothing.  So a door whose caller comes from outside the program, such as a
// C entry of a foreign runtime, gives its refusal on false.  The cell is
// trivially destructible and constinit, so it suits an object in static
// storage that outlives the state that it guards, such as a slot of a table of
// handles.  ProducerClaim of foundation/effects/Ctx.h is the claim of a state
// with a brand, and it keeps the rule that one thread holds each live claim of
// a brand.  This cell keeps no such rule.
//
// The identity
//   The cell keeps the identity of the thread as a word (calling_thread_identity
//   of foundation/effects/Ctx.h), and no integer of it leaves the cell.  A
//   thread that ends while it holds the cell leaves it held.  A new thread can
//   get the identity of the ended thread, and it then holds the cell.  So the
//   holder gives the cell up before it ends.
//
// The orders
//   try_claim takes a free cell with an exchange of acquire and release order,
//   and release frees it with a release store.  So each write of the last
//   holder happens before each access of the next holder.  is_held_by_caller
//   reads the cell with an acquire load.  On x86-64 that load is a plain mov.

#include <foundation/Platform.h>
#include <foundation/core/Atomic.h>
#include <foundation/effects/Ctx.h>

#include <cstdint>

// The cell owns no key of a context, so it stands outside the namespace host,
// whose classes own the keys.
namespace foundation::effects {

class ThreadClaimCell {
public:
    constexpr ThreadClaimCell() noexcept = default;
    ThreadClaimCell(const ThreadClaimCell&) = delete("a claim cell names one holder of one object");
    ThreadClaimCell& operator=(const ThreadClaimCell&) = delete("a claim cell names one holder of one object");
    ThreadClaimCell(ThreadClaimCell&&) = delete("a claim cell names one holder of one object");
    ThreadClaimCell& operator=(ThreadClaimCell&&) = delete("a claim cell names one holder of one object");
    ~ThreadClaimCell() = default;

    // True when the calling thread holds the cell.  A load and a comparison.
    [[nodiscard]] CRUCIBLE_INLINE bool is_held_by_caller() const noexcept {
        return holder_.load_acquire() == detail::producer_claim::calling_thread_identity();
    }

    // Takes a free cell for the calling thread.  True when the calling thread
    // holds the cell after the call, also when it held the cell before.  False
    // when another thread holds the cell, and the cell does not change then.
    [[nodiscard]] bool try_claim() noexcept {
        const std::uintptr_t caller = detail::producer_claim::calling_thread_identity();
        return holder_.cas_acq_rel(std::uintptr_t{0}, caller)
            .match([](::foundation::core::Unit) noexcept { return true; },
                   [caller](::foundation::core::CasRefusal<std::uintptr_t> refused) noexcept {
                       return refused.observed == caller;
                   });
    }

    // Gives the cell up.  True when the calling thread held the cell, and
    // false with no change when it did not.
    [[nodiscard]] bool release() noexcept {
        if (!is_held_by_caller()) return false;
        holder_.store_release(std::uintptr_t{0});
        return true;
    }

private:
    // The identity of the thread that holds the cell, or 0, which names no
    // thread.
    ::foundation::core::Atomic<std::uintptr_t> holder_{std::uintptr_t{0}};
};

}  // namespace foundation::effects
