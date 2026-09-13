#pragma once

// An MPSC ring behind fractional producer shares and one linear
// consumer permission.  The handle types carry the role, so pushing
// from the consumer side or popping from a producer side does not
// compile.
//
// The producer side is fractional because the ring is structurally
// many-producer and its membership is dynamic.  A linear producer token
// would force either a handoff between producer threads, which defeats
// concurrency, or a split into a fixed number of slots, which defeats
// dynamic membership.  The pool's share count doubles as telemetry, and
// draining it is what gives exclusive access.
//
// The consumer side stays linear because the ring's pop path assumes a
// sole consumer: the tail is consumer-owned and carries no
// synchronization, so two consumers would race on the cell reads.  The
// linear permission turns that race into a compile error.
//
// Each channel needs a UserTag of its own.  Two channels sharing a tag
// share Permission types, and their endpoints become interchangeable.
// Mint each consumer root once per program: nothing checks that at
// runtime.

#include <crucible/Platform.h>
#include <crucible/concurrent/MpscRing.h>
#include <crucible/concurrent/WorkingSet.h>
#include <crucible/permissions/Permission.h>
#include <crucible/safety/Pinned.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <type_traits>
#include <utility>

namespace crucible::concurrent {

// The triple is specialized for splitting at the foot of this file, so
// a user tag takes no per-tag boilerplate.

namespace mpsc_tag {

template <typename UserTag>
struct Whole {};
template <typename UserTag>
struct Producer {};
template <typename UserTag>
struct Consumer {};

}  // namespace mpsc_tag

template <RingValue T, std::size_t Capacity, typename UserTag = void>
class PermissionedMpscChannel : public safety::Pinned<PermissionedMpscChannel<T, Capacity, UserTag>> {
public:
    using value_type = T;
    using user_tag = UserTag;
    using whole_tag = mpsc_tag::Whole<UserTag>;
    using producer_tag = mpsc_tag::Producer<UserTag>;
    using consumer_tag = mpsc_tag::Consumer<UserTag>;

    static constexpr std::size_t channel_capacity = Capacity;

    // The producer root is minted here rather than passed in, because
    // the pool is the root of trust for its own tag.  Accepting an
    // external permission would let a caller park one in the wrong
    // pool.  The caller mints and keeps the consumer permission.

    PermissionedMpscChannel() noexcept : producer_pool_{safety::mint_permission_root<producer_tag>()} {}

    // Holds a pool share for its whole lifetime and gives it back on
    // destruction.

    class ProducerHandle {
        PermissionedMpscChannel* ch_ = nullptr;
        safety::SharedPermissionGuard<producer_tag> guard_;

        constexpr ProducerHandle(PermissionedMpscChannel& c, safety::SharedPermissionGuard<producer_tag>&& g) noexcept
            : ch_{&c}, guard_{std::move(g)} {}
        friend class PermissionedMpscChannel;

    public:
        static constexpr std::size_t per_call_working_set = lines_plus_cell_working_set_v<3, T>;

        ProducerHandle(const ProducerHandle&) =
            delete("ProducerHandle owns a Pool refcount share — copy would double-count");
        ProducerHandle& operator=(const ProducerHandle&) =
            delete("ProducerHandle owns a Pool refcount share — assignment would double-count");
        constexpr ProducerHandle(ProducerHandle&&) noexcept = default;
        // Move assignment stays deleted, because the share's lifetime
        // is fixed at construction.

        [[nodiscard, gnu::hot]] bool try_push(T item) noexcept { return ch_->ring_.try_push(std::move(item)); }

        // Snapshots.  Sound for telemetry and for deciding whether to
        // keep retrying, never for a correctness invariant.
        [[nodiscard]] bool empty_approx() const noexcept { return ch_->ring_.empty_approx(); }
        [[nodiscard]] std::size_t size_approx() const noexcept { return ch_->ring_.size_approx(); }
        [[nodiscard]] static constexpr std::size_t capacity() noexcept { return Capacity; }
    };

    class ConsumerHandle {
        // A reference rather than a pointer, because a handle binds to
        // one channel for life.  The reference also deletes move
        // assignment, which matters: a defaulted move of an empty
        // Permission is a no-op, so the source and the target would
        // both go on claiming the linear token.
        PermissionedMpscChannel& ch_;
        [[no_unique_address]] safety::Permission<consumer_tag> perm_;

        constexpr ConsumerHandle(PermissionedMpscChannel& c, safety::Permission<consumer_tag>&& p) noexcept
            : ch_{c}, perm_{std::move(p)} {}
        friend class PermissionedMpscChannel;

    public:
        static constexpr std::size_t per_call_working_set = lines_plus_cell_working_set_v<3, T>;

        ConsumerHandle(const ConsumerHandle&) =
            delete("ConsumerHandle owns the Consumer Permission — copy would duplicate the linear token");
        ConsumerHandle& operator=(const ConsumerHandle&) =
            delete("ConsumerHandle owns the Consumer Permission — assignment would overwrite the linear token");
        constexpr ConsumerHandle(ConsumerHandle&&) noexcept = default;
        ConsumerHandle& operator=(ConsumerHandle&&) = delete(
            "ConsumerHandle binds to ONE channel for life — rebinding would orphan the original Permission and silently allow a second consumer to coexist (MpscRing's try_pop is single-consumer-only)");

        [[nodiscard, gnu::hot]] std::optional<T> try_pop() noexcept { return ch_.ring_.try_pop(); }

        [[nodiscard]] bool empty_approx() const noexcept { return ch_.ring_.empty_approx(); }
        [[nodiscard]] std::size_t size_approx() const noexcept { return ch_.ring_.size_approx(); }
        [[nodiscard]] static constexpr std::size_t capacity() noexcept { return Capacity; }
    };

    // Lends a pool share, and refuses while an exclusive transition is
    // in flight.  Several producer handles coexisting is the point of
    // the fractional side.
    [[nodiscard]] std::optional<ProducerHandle> producer() noexcept {
        auto guard = producer_pool_.lend();
        if (!guard) return std::nullopt;
        return ProducerHandle{*this, std::move(*guard)};
    }

    [[nodiscard]] ConsumerHandle consumer(safety::Permission<consumer_tag>&& perm) noexcept {
        return ConsumerHandle{*this, std::move(perm)};
    }

    // Runs the body with every producer out, which is what a reset, a
    // resize or a migration needs.  Returns false when producers were
    // still out and the body did not run.  A producer can be lent again
    // once the body returns.
    template <typename Body>
        requires std::is_invocable_v<Body>
    bool with_drained_access(Body&& body) noexcept(std::is_nothrow_invocable_v<Body>) {
        auto upgrade = producer_pool_.try_upgrade();
        if (!upgrade) return false;
        std::forward<Body>(body)();
        producer_pool_.deposit_exclusive(std::move(*upgrade));
        return true;
    }

    [[nodiscard]] std::uint64_t outstanding_producers() const noexcept { return producer_pool_.outstanding(); }

    [[nodiscard]] bool is_exclusive_active() const noexcept { return producer_pool_.is_exclusive_out(); }

    [[nodiscard]] bool empty_approx() const noexcept { return ring_.empty_approx(); }
    [[nodiscard]] std::size_t size_approx() const noexcept { return ring_.size_approx(); }
    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return Capacity; }

private:
    MpscRing<T, Capacity> ring_;
    safety::SharedPermissionPool<producer_tag> producer_pool_;
};

}  // namespace crucible::concurrent

// Both the binary and the variadic split forms are specialized, so a
// caller can reach for either one.

namespace crucible::safety {

template <typename UserTag>
struct splits_into<concurrent::mpsc_tag::Whole<UserTag>, concurrent::mpsc_tag::Producer<UserTag>,
                   concurrent::mpsc_tag::Consumer<UserTag>> : std::true_type {};

template <typename UserTag>
struct splits_into_pack<concurrent::mpsc_tag::Whole<UserTag>, concurrent::mpsc_tag::Producer<UserTag>,
                        concurrent::mpsc_tag::Consumer<UserTag>> : std::true_type {};

template <typename UserTag>
struct splits_into_authoring_witness<concurrent::mpsc_tag::Whole<UserTag>, concurrent::mpsc_tag::Producer<UserTag>,
                                     concurrent::mpsc_tag::Consumer<UserTag>> : std::true_type {};

template <typename UserTag>
struct splits_into_pack_authoring_witness<concurrent::mpsc_tag::Whole<UserTag>, concurrent::mpsc_tag::Producer<UserTag>,
                                          concurrent::mpsc_tag::Consumer<UserTag>> : std::true_type {};

}  // namespace crucible::safety
