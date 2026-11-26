#pragma once

// A counter that says "N items processed" is a lie about publication when the
// bump and the publication of those items' side effects sit in different stages
// of a pipeline: a reader that spins on the counter is then released before the
// side effects it was waiting for are visible.  This cell makes the counter
// writable only from the stage that publishes, so the two cannot drift apart.
//
// WriteAuth names the token of the stage that publishes.  Each write and the
// read of the writer take the token.  The token is closed and states
// thread_bound{} and not_shareable{}, so only a mint of its owner builds one,
// and no other thread holds it or reads it (the build of a token of
// foundation/Transfer.h).  The alias write_auth_type ties the token to the
// cell, so the door check of synchronized{} reads the token as the proof of
// the one writer.

#include <foundation/Platform.h>
#include <foundation/Transfer.h>
#include <foundation/core/TypeParts.h>

#include <atomic>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace fixy::handle {

// Declare a fresh Tag per pipeline.  Two pipelines that reuse one Tag share a
// cell type even when their WriteAuth differs.
//
// The count is private, each door reads or writes it atomically, and each
// write takes the token of the one writer, so two threads can use one cell at
// the same time, and the class states synchronized{} (foundation/Transfer.h).
// No door keeps a borrow that a caller gives it, so the cell states
// synchronized_by_proof{} too (foundation/core/TypeParts.h), and a body that
// captures the cell by reference passes the store check.
template <typename Tag, typename WriteAuth>
class[[= ::foundation::transfer::synchronized{}]][[= ::foundation::core::synchronized_by_proof{}]] PublishCommitCell {
    alignas(64) std::atomic<uint64_t> value_{0};

public:
    using tag_type = Tag;
    using write_auth_type = WriteAuth;

    // The release half of acq_rel pairs with load_acquire below.
    uint64_t bump_by(WriteAuth const& /*auth*/, uint64_t delta) noexcept {
        return value_.fetch_add(delta, std::memory_order_acq_rel);
    }

    uint64_t bump(WriteAuth const& auth) noexcept { return bump_by(auth, 1); }

    // The read of the one writer of its own count: the holder of the token
    // stores each value of the cell, so the read needs no order.  A reader on
    // another thread reads the count with load_acquire, because a relaxed read
    // is only for the own variable of the thread.
    [[nodiscard]] uint64_t peek_relaxed(WriteAuth const& /*auth*/) const noexcept {
        return value_.load(std::memory_order_relaxed);
    }

    constexpr PublishCommitCell() noexcept = default;

    PublishCommitCell(const PublishCommitCell&) = delete("PublishCommitCell owns the channel identity; not copyable");
    PublishCommitCell&
    operator=(const PublishCommitCell&) = delete("PublishCommitCell owns the channel identity; not copyable");
    PublishCommitCell(PublishCommitCell&&) = delete("interior atomic crosses thread boundary; cannot move");
    PublishCommitCell& operator=(PublishCommitCell&&) = delete("interior atomic crosses thread boundary; cannot move");

    // Every write the publishing stage made before its bump is visible to a
    // reader that observes the bumped value through this load.  It and the
    // read of the writer, peek_relaxed, are the only reads: a read that takes
    // its order as an argument would admit a release order, which no load
    // can have.
    //
    // No read here is gnu::pure.  The publishing stage changes the value
    // between two reads, and a pure read lets the compiler fold the reads of
    // a wait loop into one.
    [[nodiscard]] uint64_t load_acquire() const noexcept { return value_.load(std::memory_order_acquire); }
};

}  // namespace fixy::handle
