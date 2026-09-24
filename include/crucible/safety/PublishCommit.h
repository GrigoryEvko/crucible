#pragma once

// A counter that says "N items processed" is a lie about publication when the
// bump and the publication of those items' side effects sit in different stages
// of a pipeline: a reader that spins on the counter is then released before the
// side effects it was waiting for are visible.  This cell makes the counter
// writable only from the stage that publishes, so the two cannot drift apart.
//
// WriteAuth names the type that performs the publication.  The friend list is
// the whole gate, so there is no token to thread through call sites, and a
// second writer costs a second friend declaration that a reviewer sees.

#include <crucible/Platform.h>

#include <atomic>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace crucible::safety {

// Declare a fresh Tag per pipeline.  Two pipelines that reuse one Tag share a
// cell type even when their WriteAuth differs.
template <typename Tag, typename WriteAuth>
class PublishCommitCell {
    alignas(64) std::atomic<uint64_t> value_{0};

    friend WriteAuth;

    // The release half of acq_rel pairs with load_acquire below.
    uint64_t bump_by(uint64_t delta) noexcept { return value_.fetch_add(delta, std::memory_order_acq_rel); }

    uint64_t bump() noexcept { return bump_by(1); }

public:
    using tag_type = Tag;
    using write_auth_type = WriteAuth;

    constexpr PublishCommitCell() noexcept = default;

    PublishCommitCell(const PublishCommitCell&) = delete("PublishCommitCell owns the channel identity; not copyable");
    PublishCommitCell&
    operator=(const PublishCommitCell&) = delete("PublishCommitCell owns the channel identity; not copyable");
    PublishCommitCell(PublishCommitCell&&) = delete("interior atomic crosses thread boundary; cannot move");
    PublishCommitCell& operator=(PublishCommitCell&&) = delete("interior atomic crosses thread boundary; cannot move");

    // Every write the publishing stage made before its bump is visible to a
    // reader that observes the bumped value through this load.
    [[nodiscard, gnu::pure]] uint64_t load_acquire() const noexcept { return value_.load(std::memory_order_acquire); }

    // Carries no synchronization.  Read the counter this way for reporting
    // only, never to conclude that the published writes are visible.
    [[nodiscard, gnu::pure]] uint64_t peek_relaxed() const noexcept { return value_.load(std::memory_order_relaxed); }

    [[nodiscard, gnu::pure]] uint64_t get() const noexcept { return load_acquire(); }

    [[nodiscard, gnu::pure]] uint64_t load(std::memory_order order = std::memory_order_acquire) const noexcept {
        return value_.load(order);
    }
};

namespace publish_commit_detail {

struct ProbeTag {};
struct ProbeAuth {};

static_assert(std::is_trivially_destructible_v<PublishCommitCell<ProbeTag, ProbeAuth>>);
static_assert(!std::is_move_constructible_v<PublishCommitCell<ProbeTag, ProbeAuth>>);
static_assert(!std::is_copy_constructible_v<PublishCommitCell<ProbeTag, ProbeAuth>>);
static_assert(alignof(PublishCommitCell<ProbeTag, ProbeAuth>) == 64);

}  // namespace publish_commit_detail

}  // namespace crucible::safety
