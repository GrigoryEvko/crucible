#pragma once

// A grid of independent SPSC rings behind one linear permission per
// producer slot and per consumer slot.  Every permission descends from
// a single whole-tag root.  No pool is needed, since each slot has a
// unique owner.
//
// A handle's slot index lives in its type, so pushing takes no index
// argument.  The rejected alternative is a runtime index, which looks
// more flexible and gives up the discipline entirely: two threads can
// pass the same producer index at once with no diagnostic, and a
// consumer index passed where a producer index belongs is just another
// integer.  With the index in the type, each slot has its own handle
// type, the linear permission stops a second handle for that slot, and
// no index can be supplied at the call.
//
// Each grid needs a UserTag of its own.  Two grids sharing a tag share
// Permission types, and their endpoints become interchangeable.  Mint
// each whole tag's root once per program: nothing checks that at
// runtime.

#include <crucible/Platform.h>
#include <crucible/concurrent/ShardedGrid.h>
#include <crucible/permissions/Permission.h>
#include <crucible/safety/PermissionGridGenerator.h>
#include <crucible/safety/PermissionTreeGenerator.h>
#include <crucible/safety/_Pinned.h>

#include <cstddef>
#include <optional>
#include <type_traits>
#include <utility>

namespace crucible::concurrent {

// The slot tags come straight from the permission-grid generator, so a
// user tag takes no per-tag boilerplate and the generator hands back
// tuples that already match these types.

namespace grid_tag {

template <typename UserTag>
struct Whole {};

template <typename UserTag, std::size_t I>
using Producer = safety::Producer<Whole<UserTag>, I>;

template <typename UserTag, std::size_t J>
using Consumer = safety::Consumer<Whole<UserTag>, J>;

}  // namespace grid_tag

template <SpscValue T, std::size_t M, std::size_t N, std::size_t Capacity, typename UserTag = void,
          typename Routing = RoundRobinRouting>
class PermissionedShardedGrid : public safety::Pinned<PermissionedShardedGrid<T, M, N, Capacity, UserTag, Routing>> {
public:
    using value_type = T;
    using user_tag = UserTag;
    using whole_tag = grid_tag::Whole<UserTag>;

    static constexpr std::size_t num_producers = M;
    static constexpr std::size_t num_consumers = N;
    static constexpr std::size_t shard_capacity = Capacity;

    PermissionedShardedGrid() noexcept = default;

    template <std::size_t I>
    class ProducerHandle {
        static_assert(I < M, "Producer slot index out of range");

        // A reference rather than a pointer, because a handle binds to
        // one grid for life.  The reference also deletes move
        // assignment, which matters: a defaulted move of an empty
        // Permission is a no-op, so the source and the target would
        // both go on claiming the linear token.
        PermissionedShardedGrid& grid_;
        [[no_unique_address]] safety::Permission<grid_tag::Producer<UserTag, I>> perm_;

        constexpr ProducerHandle(PermissionedShardedGrid& g,
                                 safety::Permission<grid_tag::Producer<UserTag, I>>&& p) noexcept
            : grid_{g}, perm_{std::move(p)} {}
        friend class PermissionedShardedGrid;

    public:
        ProducerHandle(const ProducerHandle&) = delete(
            "ProducerHandle owns the slot's Producer Permission — copy would duplicate the linear token, allowing two threads to share a single SPSC slot (data race on the inner SpscRing)");
        ProducerHandle& operator=(const ProducerHandle&) =
            delete("ProducerHandle owns the slot's Producer Permission — assignment would overwrite the linear token");
        constexpr ProducerHandle(ProducerHandle&&) noexcept = default;
        ProducerHandle& operator=(ProducerHandle&&) =
            delete("ProducerHandle binds to ONE shard slot for life — the slot index is part of the type");

        static constexpr std::size_t shard_index = I;

        // The routing policy picks the consumer column.
        [[nodiscard, gnu::hot]] bool try_push(const T& item) noexcept { return grid_.grid_.try_push(I, item); }

        // Snapshots over this slot's row.  Sound for telemetry and for
        // deciding whether to keep retrying, never for a correctness
        // invariant.
        [[nodiscard]] std::size_t size_approx() const noexcept {
            std::size_t total = 0;
            for (std::size_t j = 0; j < N; ++j) {
                total += grid_.grid_.size_approx(I, j);
            }
            return total;
        }
        [[nodiscard]] bool empty_approx() const noexcept {
            for (std::size_t j = 0; j < N; ++j) {
                if (grid_.grid_.size_approx(I, j) != 0) return false;
            }
            return true;
        }
        [[nodiscard]] static constexpr std::size_t capacity() noexcept { return Capacity; }
    };

    template <std::size_t J>
    class ConsumerHandle {
        static_assert(J < N, "Consumer slot index out of range");

        PermissionedShardedGrid& grid_;
        [[no_unique_address]] safety::Permission<grid_tag::Consumer<UserTag, J>> perm_;

        constexpr ConsumerHandle(PermissionedShardedGrid& g,
                                 safety::Permission<grid_tag::Consumer<UserTag, J>>&& p) noexcept
            : grid_{g}, perm_{std::move(p)} {}
        friend class PermissionedShardedGrid;

    public:
        ConsumerHandle(const ConsumerHandle&) = delete(
            "ConsumerHandle owns the slot's Consumer Permission — copy would duplicate the linear token, allowing two threads to share a single SPSC consumer slot (data race on column drain)");
        ConsumerHandle& operator=(const ConsumerHandle&) =
            delete("ConsumerHandle owns the slot's Consumer Permission — assignment would overwrite the linear token");
        constexpr ConsumerHandle(ConsumerHandle&&) noexcept = default;
        ConsumerHandle& operator=(ConsumerHandle&&) =
            delete("ConsumerHandle binds to ONE shard slot for life — the slot index is part of the type");

        static constexpr std::size_t shard_index = J;

        [[nodiscard, gnu::hot]] std::optional<T> try_pop() noexcept { return grid_.grid_.try_pop(J); }

        // Snapshots over this slot's column.  Sound for telemetry and
        // for deciding whether to keep retrying, never for a
        // correctness invariant.
        [[nodiscard]] std::size_t size_approx() const noexcept {
            std::size_t total = 0;
            for (std::size_t i = 0; i < M; ++i) {
                total += grid_.grid_.size_approx(i, J);
            }
            return total;
        }
        [[nodiscard]] bool empty_approx() const noexcept {
            for (std::size_t i = 0; i < M; ++i) {
                if (grid_.grid_.size_approx(i, J) != 0) return false;
            }
            return true;
        }
        [[nodiscard]] static constexpr std::size_t capacity() noexcept { return Capacity; }
    };

    template <std::size_t I>
    [[nodiscard]] constexpr ProducerHandle<I>
    producer(safety::Permission<grid_tag::Producer<UserTag, I>>&& perm) noexcept {
        static_assert(I < M, "producer<I>(): I must be less than M");
        return ProducerHandle<I>{*this, std::move(perm)};
    }

    template <std::size_t J>
    [[nodiscard]] constexpr ConsumerHandle<J>
    consumer(safety::Permission<grid_tag::Consumer<UserTag, J>>&& perm) noexcept {
        static_assert(J < N, "consumer<J>(): J must be less than N");
        return ConsumerHandle<J>{*this, std::move(perm)};
    }

    // Scoped exclusive access to the whole grid.  Every slot holds a
    // linear token and there is no refcount to drain, so surrendering
    // the recombined whole permission is itself the proof that no
    // handle is alive on any shard.  The whole permission comes back so
    // the caller can split it again, and the exchange is type-level
    // with no atomic operation.
    template <typename Body>
        requires std::is_invocable_v<Body>
    [[nodiscard]] safety::Permission<whole_tag>
    with_recombined_access(safety::Permission<whole_tag>&& whole,
                           Body&& body) noexcept(std::is_nothrow_invocable_v<Body>) {
        std::forward<Body>(body)();
        return std::move(whole);
    }

    // The cells are independent rings, so a single global size means
    // little.  This form reads one cell.
    [[nodiscard]] std::size_t size_approx(std::size_t producer_id, std::size_t consumer_id) const noexcept {
        return grid_.size_approx(producer_id, consumer_id);
    }

    // These walk every cell, so a hot path wants the per-cell form.
    [[nodiscard]] std::size_t size_approx() const noexcept { return grid_.size_approx(); }
    [[nodiscard]] bool empty_approx() const noexcept { return grid_.empty_approx(); }

    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return M * N * Capacity; }

    // Always false, and present only so this grid matches the shape of
    // the pool-backed channels.  There is no exclusivity flag to read:
    // the linear permissions on every slot are what prove single
    // ownership.
    [[nodiscard]] static constexpr bool is_exclusive_active() noexcept { return false; }

private:
    ShardedSpscGrid<T, M, N, Capacity, Routing> grid_;
};

}  // namespace crucible::concurrent
