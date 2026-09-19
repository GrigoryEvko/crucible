#pragma once

// An SPSC ring behind one linear Permission per endpoint.  The handle
// types carry the role, so pushing from the consumer side or popping
// from the producer side does not compile, and the move-only Permission
// keeps a channel to one producer and one consumer at a time.
//
// Each channel needs a UserTag of its own.  Two channels sharing a tag
// share Permission types, and their endpoints become interchangeable.
// Mint each whole tag's root once per program: nothing checks that at
// runtime.

#include <crucible/Platform.h>
#include <crucible/concurrent/WorkingSet.h>
#include <crucible/concurrent/SpscRing.h>
#include <crucible/permissions/_Permission.h>
#include <crucible/safety/_Pinned.h>

#include <cstddef>
#include <optional>
#include <type_traits>
#include <utility>

namespace crucible::concurrent {

// The triple is specialized for splitting at the foot of this file, so
// a user tag takes no per-tag boilerplate.

namespace spsc_tag {

template <typename UserTag>
struct Whole {};
template <typename UserTag>
struct Producer {};
template <typename UserTag>
struct Consumer {};

}  // namespace spsc_tag

template <SpscValue T, std::size_t Capacity, typename UserTag = void>
class PermissionedSpscChannel : public safety::Pinned<PermissionedSpscChannel<T, Capacity, UserTag>> {
public:
    using value_type = T;
    using user_tag = UserTag;
    using whole_tag = spsc_tag::Whole<UserTag>;
    using producer_tag = spsc_tag::Producer<UserTag>;
    using consumer_tag = spsc_tag::Consumer<UserTag>;

    static constexpr std::size_t channel_capacity = Capacity;

    // The channel's identity is its address, since the ring's atomics
    // depend on a stable one.

    PermissionedSpscChannel() noexcept = default;

    class ProducerHandle {
        // A reference rather than a pointer, because a handle binds to
        // one channel for life.  The reference also deletes move
        // assignment, which matters: a defaulted move of an empty
        // Permission is a no-op, so the source and the target would
        // both go on claiming the linear token.
        PermissionedSpscChannel& ch_;
        [[no_unique_address]] safety::Permission<producer_tag> perm_;

        constexpr ProducerHandle(PermissionedSpscChannel& c, safety::Permission<producer_tag>&& p) noexcept
            : ch_{c}, perm_{std::move(p)} {}
        friend class PermissionedSpscChannel;

    public:
        static constexpr std::size_t per_call_working_set = lines_plus_cell_working_set_v<2, T>;

        ProducerHandle(const ProducerHandle&) =
            delete("ProducerHandle owns the Producer Permission — copy would duplicate the linear token");
        ProducerHandle& operator=(const ProducerHandle&) =
            delete("ProducerHandle owns the Producer Permission — assignment would overwrite the linear token");
        constexpr ProducerHandle(ProducerHandle&&) noexcept = default;
        // The reference member already deletes this implicitly.  Saying
        // so explicitly puts the reason in the diagnostic instead of
        // pointing at an implicitly-deleted special member.
        ProducerHandle& operator=(ProducerHandle&&) = delete(
            "ProducerHandle binds to ONE channel for life — rebinding would orphan the original Permission and silently allow a second producer to coexist");

        [[nodiscard, gnu::hot]] bool try_push(const T& item) noexcept { return ch_.ring_.try_push(item); }

        // Snapshots.  Sound for telemetry and for deciding whether to
        // keep retrying, never for a correctness invariant.
        [[nodiscard]] bool empty_approx() const noexcept { return ch_.ring_.empty_approx(); }
        [[nodiscard]] std::size_t size_approx() const noexcept { return ch_.ring_.size_approx(); }
        [[nodiscard]] static constexpr std::size_t capacity() noexcept { return Capacity; }
    };

    class ConsumerHandle {
        // A reference for the same reason as in ProducerHandle.
        PermissionedSpscChannel& ch_;
        [[no_unique_address]] safety::Permission<consumer_tag> perm_;

        constexpr ConsumerHandle(PermissionedSpscChannel& c, safety::Permission<consumer_tag>&& p) noexcept
            : ch_{c}, perm_{std::move(p)} {}
        friend class PermissionedSpscChannel;

    public:
        static constexpr std::size_t per_call_working_set = lines_plus_cell_working_set_v<2, T>;

        ConsumerHandle(const ConsumerHandle&) =
            delete("ConsumerHandle owns the Consumer Permission — copy would duplicate the linear token");
        ConsumerHandle& operator=(const ConsumerHandle&) =
            delete("ConsumerHandle owns the Consumer Permission — assignment would overwrite the linear token");
        constexpr ConsumerHandle(ConsumerHandle&&) noexcept = default;
        ConsumerHandle& operator=(ConsumerHandle&&) = delete(
            "ConsumerHandle binds to ONE channel for life — rebinding would orphan the original Permission and silently allow a second consumer to coexist");

        [[nodiscard, gnu::hot]] std::optional<T> try_pop() noexcept { return ch_.ring_.try_pop(); }

        [[nodiscard]] bool empty_approx() const noexcept { return ch_.ring_.empty_approx(); }
        [[nodiscard]] std::size_t size_approx() const noexcept { return ch_.ring_.size_approx(); }
        [[nodiscard]] static constexpr std::size_t capacity() noexcept { return Capacity; }
    };

    [[nodiscard]] ProducerHandle producer(safety::Permission<producer_tag>&& perm) noexcept {
        return ProducerHandle{*this, std::move(perm)};
    }

    [[nodiscard]] ConsumerHandle consumer(safety::Permission<consumer_tag>&& perm) noexcept {
        return ConsumerHandle{*this, std::move(perm)};
    }

    // Scoped exclusive access to the ring.  Both endpoints here hold
    // linear tokens and there is no refcount to drain, so surrendering
    // the recombined whole permission is itself the proof that no
    // handle is alive.  The pool-backed channels instead drain their
    // atomic state and take no permission.  The whole permission comes
    // back so the caller can split it again for the next session, and
    // the whole exchange is type-level with no atomic operation.
    template <typename Body>
        requires std::is_invocable_v<Body>
    [[nodiscard]] safety::Permission<whole_tag>
    with_recombined_access(safety::Permission<whole_tag>&& whole,
                           Body&& body) noexcept(std::is_nothrow_invocable_v<Body>) {
        std::forward<Body>(body)();
        return std::move(whole);
    }

    [[nodiscard]] bool empty_approx() const noexcept { return ring_.empty_approx(); }
    [[nodiscard]] std::size_t size_approx() const noexcept { return ring_.size_approx(); }
    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return Capacity; }

    // Always false, and present only so this channel matches the shape
    // of the pool-backed ones.  There is no exclusivity flag to read:
    // the linear permissions are what prove single ownership.
    [[nodiscard]] static constexpr bool is_exclusive_active() noexcept { return false; }

private:
    SpscRing<T, Capacity> ring_;
};

}  // namespace crucible::concurrent

// Both the binary and the variadic split forms are specialized, so a
// caller can reach for either one.

namespace crucible::safety {

template <typename UserTag>
struct splits_into<concurrent::spsc_tag::Whole<UserTag>, concurrent::spsc_tag::Producer<UserTag>,
                   concurrent::spsc_tag::Consumer<UserTag>> : std::true_type {};

template <typename UserTag>
struct splits_into_pack<concurrent::spsc_tag::Whole<UserTag>, concurrent::spsc_tag::Producer<UserTag>,
                        concurrent::spsc_tag::Consumer<UserTag>> : std::true_type {};

template <typename UserTag>
struct splits_into_authoring_witness<concurrent::spsc_tag::Whole<UserTag>, concurrent::spsc_tag::Producer<UserTag>,
                                     concurrent::spsc_tag::Consumer<UserTag>> : std::true_type {};

template <typename UserTag>
struct splits_into_pack_authoring_witness<concurrent::spsc_tag::Whole<UserTag>, concurrent::spsc_tag::Producer<UserTag>,
                                          concurrent::spsc_tag::Consumer<UserTag>> : std::true_type {};

}  // namespace crucible::safety
