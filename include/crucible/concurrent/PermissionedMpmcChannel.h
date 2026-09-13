#pragma once

// An MPMC ring behind fractional shares on both sides.  The handle
// types carry the role, so pushing from a consumer side or popping from
// a producer side does not compile.
//
// The two pools are independent state machines with their own share
// counts and their own exclusivity bits.  Draining one side does not
// have to disturb the other, and their state words sit on separate
// cache lines, so lending a producer share does not touch the line the
// consumers contend for.
//
// Each channel needs a UserTag of its own.  Two channels sharing a tag
// share Permission types, and their endpoints become interchangeable.

#include <crucible/Platform.h>
#include <crucible/concurrent/MpmcRing.h>
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

namespace mpmc_tag {

template <typename UserTag>
struct Whole {};
template <typename UserTag>
struct Producer {};
template <typename UserTag>
struct Consumer {};

}  // namespace mpmc_tag

// A handle carries one of these as a type-state.  Closing consumes the
// Active handle and yields a Closed one, whose only remaining operation
// is destruction.  Observation stays available in both states, because
// reading a diagnostic does not advance the protocol.

namespace mpmc_session {

struct Active {};
struct Closed {};

}  // namespace mpmc_session

template <MpmcValue T, std::size_t Capacity, typename UserTag = void>
class PermissionedMpmcChannel : public safety::Pinned<PermissionedMpmcChannel<T, Capacity, UserTag>> {
public:
    using value_type = T;
    using user_tag = UserTag;
    using whole_tag = mpmc_tag::Whole<UserTag>;
    using producer_tag = mpmc_tag::Producer<UserTag>;
    using consumer_tag = mpmc_tag::Consumer<UserTag>;

    static constexpr std::size_t channel_capacity = Capacity;

    // Both roots are minted here rather than passed in, because a pool
    // is the root of trust for its own tag.  Accepting an external
    // permission would let a caller park one in the wrong pool.

    PermissionedMpmcChannel() noexcept
        : producer_pool_{safety::mint_permission_root<producer_tag>()},
          consumer_pool_{safety::mint_permission_root<consumer_tag>()} {}

    // Holds a pool share for its whole lifetime and gives it back on
    // destruction.

    template <typename State = mpmc_session::Active>
    class ProducerHandleT {
        PermissionedMpmcChannel* ch_ = nullptr;
        safety::SharedPermissionGuard<producer_tag> guard_;

        constexpr ProducerHandleT(PermissionedMpmcChannel& c, safety::SharedPermissionGuard<producer_tag>&& g) noexcept
            : ch_{&c}, guard_{std::move(g)} {}
        friend class PermissionedMpmcChannel;
        // Cross-state friendship is what lets close construct the
        // Closed handle out of this one's moved-out members.
        template <typename Other>
        friend class ProducerHandleT;

    public:
        using session_state = State;
        static constexpr std::size_t per_call_working_set = lines_plus_cell_working_set_v<3, T>;

        ProducerHandleT(const ProducerHandleT&) =
            delete("ProducerHandle owns a producer-pool refcount share — copy would double-count");
        ProducerHandleT& operator=(const ProducerHandleT&) =
            delete("ProducerHandle owns a producer-pool refcount share — assignment would double-count");
        constexpr ProducerHandleT(ProducerHandleT&&) noexcept = default;

        // A false result covers both a full ring and a transient
        // failure to place the item, so a caller that wants the item
        // delivered retries.
        [[nodiscard, gnu::hot]] bool try_push(const T& item) noexcept
            requires std::is_same_v<State, mpmc_session::Active>
        {
            return ch_->ring_.try_push(item);
        }

        [[nodiscard, gnu::hot]] std::size_t try_push_batch(std::span<const T> items) noexcept
            requires std::is_same_v<State, mpmc_session::Active>
        {
            return ch_->ring_.try_push_batch(items);
        }

        // The pool share travels into the Closed handle and is released
        // when that handle dies, so closing costs the pool the same as
        // dropping an open handle.
        [[nodiscard]] ProducerHandleT<mpmc_session::Closed> close() && noexcept
            requires std::is_same_v<State, mpmc_session::Active>
        {
            return ProducerHandleT<mpmc_session::Closed>{*ch_, std::move(guard_)};
        }

        // Snapshots.  Sound for telemetry and for deciding whether to
        // keep retrying, never for a correctness invariant.
        [[nodiscard]] bool empty_approx() const noexcept { return ch_->ring_.empty_approx(); }
        [[nodiscard]] std::size_t size_approx() const noexcept { return ch_->ring_.size_approx(); }
        [[nodiscard]] static constexpr std::size_t capacity() noexcept { return Capacity; }
    };

    template <typename State = mpmc_session::Active>
    class ConsumerHandleT {
        PermissionedMpmcChannel* ch_ = nullptr;
        safety::SharedPermissionGuard<consumer_tag> guard_;

        constexpr ConsumerHandleT(PermissionedMpmcChannel& c, safety::SharedPermissionGuard<consumer_tag>&& g) noexcept
            : ch_{&c}, guard_{std::move(g)} {}
        friend class PermissionedMpmcChannel;
        template <typename Other>
        friend class ConsumerHandleT;

    public:
        using session_state = State;
        static constexpr std::size_t per_call_working_set = lines_plus_cell_working_set_v<3, T>;

        ConsumerHandleT(const ConsumerHandleT&) =
            delete("ConsumerHandle owns a consumer-pool refcount share — copy would double-count");
        ConsumerHandleT& operator=(const ConsumerHandleT&) =
            delete("ConsumerHandle owns a consumer-pool refcount share — assignment would double-count");
        constexpr ConsumerHandleT(ConsumerHandleT&&) noexcept = default;

        // A nullopt result covers both an empty ring and a transient
        // failure to claim an item, so a caller that wants an item
        // retries.
        [[nodiscard, gnu::hot]] std::optional<T> try_pop() noexcept
            requires std::is_same_v<State, mpmc_session::Active>
        {
            return ch_->ring_.try_pop();
        }

        [[nodiscard, gnu::hot]] std::size_t try_pop_batch(std::span<T> out) noexcept
            requires std::is_same_v<State, mpmc_session::Active>
        {
            return ch_->ring_.try_pop_batch(out);
        }

        [[nodiscard]] ConsumerHandleT<mpmc_session::Closed> close() && noexcept
            requires std::is_same_v<State, mpmc_session::Active>
        {
            return ConsumerHandleT<mpmc_session::Closed>{*ch_, std::move(guard_)};
        }

        [[nodiscard]] bool empty_approx() const noexcept { return ch_->ring_.empty_approx(); }
        [[nodiscard]] std::size_t size_approx() const noexcept { return ch_->ring_.size_approx(); }
        [[nodiscard]] static constexpr std::size_t capacity() noexcept { return Capacity; }
    };

    // The unqualified names are the Active specializations, and a
    // Closed handle is reachable only by closing an Active one.
    using ProducerHandle = ProducerHandleT<mpmc_session::Active>;
    using ConsumerHandle = ConsumerHandleT<mpmc_session::Active>;
    using ProducerHandleClosed = ProducerHandleT<mpmc_session::Closed>;
    using ConsumerHandleClosed = ConsumerHandleT<mpmc_session::Closed>;

    // Lends a share, and refuses while that side is held exclusively.
    [[nodiscard]] std::optional<ProducerHandle> producer() noexcept {
        auto guard = producer_pool_.lend();
        if (!guard) return std::nullopt;
        return ProducerHandle{*this, std::move(*guard)};
    }

    [[nodiscard]] std::optional<ConsumerHandle> consumer() noexcept {
        auto guard = consumer_pool_.lend();
        if (!guard) return std::nullopt;
        return ConsumerHandle{*this, std::move(*guard)};
    }

    // Runs the body with no live handle on either side, which is what a
    // reset, a resize or a migration needs.  Both pools are taken
    // exclusively, and neither call blocks or spins.  Returns false
    // when either side was still out and the body did not run.
    template <typename Body>
        requires std::is_invocable_v<Body>
    bool with_drained_access(Body&& body) noexcept(std::is_nothrow_invocable_v<Body>) {
        auto prod_upgrade = producer_pool_.try_upgrade();
        if (!prod_upgrade) return false;

        auto cons_upgrade = consumer_pool_.try_upgrade();
        if (!cons_upgrade) {
            // The producer upgrade has to go back, or the exclusive
            // permission leaks and the producer side stays closed for
            // the life of the channel.
            producer_pool_.deposit_exclusive(std::move(*prod_upgrade));
            return false;
        }

        std::forward<Body>(body)();

        // The pools are independent, so the order of these two carries
        // no correctness weight.  Reverse of acquisition is the habit.
        consumer_pool_.deposit_exclusive(std::move(*cons_upgrade));
        producer_pool_.deposit_exclusive(std::move(*prod_upgrade));
        return true;
    }

    [[nodiscard]] std::uint64_t outstanding_producers() const noexcept { return producer_pool_.outstanding(); }
    [[nodiscard]] std::uint64_t outstanding_consumers() const noexcept { return consumer_pool_.outstanding(); }
    // A disjunction, because the only thing that takes either pool
    // exclusively takes both, and the rollback path leaves one held
    // briefly on its own.
    [[nodiscard]] bool is_exclusive_active() const noexcept {
        return producer_pool_.is_exclusive_out() || consumer_pool_.is_exclusive_out();
    }
    [[nodiscard]] bool empty_approx() const noexcept { return ring_.empty_approx(); }
    [[nodiscard]] std::size_t size_approx() const noexcept { return ring_.size_approx(); }
    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return Capacity; }

private:
    MpmcRing<T, Capacity> ring_;
    safety::SharedPermissionPool<producer_tag> producer_pool_;
    safety::SharedPermissionPool<consumer_tag> consumer_pool_;
};

}  // namespace crucible::concurrent

// Both the binary and the variadic split forms are specialized, so a
// caller can reach for either one.

namespace crucible::safety {

template <typename UserTag>
struct splits_into<concurrent::mpmc_tag::Whole<UserTag>, concurrent::mpmc_tag::Producer<UserTag>,
                   concurrent::mpmc_tag::Consumer<UserTag>> : std::true_type {};

template <typename UserTag>
struct splits_into_pack<concurrent::mpmc_tag::Whole<UserTag>, concurrent::mpmc_tag::Producer<UserTag>,
                        concurrent::mpmc_tag::Consumer<UserTag>> : std::true_type {};

template <typename UserTag>
struct splits_into_authoring_witness<concurrent::mpmc_tag::Whole<UserTag>, concurrent::mpmc_tag::Producer<UserTag>,
                                     concurrent::mpmc_tag::Consumer<UserTag>> : std::true_type {};

template <typename UserTag>
struct splits_into_pack_authoring_witness<concurrent::mpmc_tag::Whole<UserTag>, concurrent::mpmc_tag::Producer<UserTag>,
                                          concurrent::mpmc_tag::Consumer<UserTag>> : std::true_type {};

}  // namespace crucible::safety
