#pragma once

// ═══════════════════════════════════════════════════════════════════
// PermissionedShardedGrid<T, M, N, Capacity, UserTag, Routing>
// — M producers × N consumers worked example
//
// Wraps ShardedSpscGrid<T, M, N, Capacity, Routing> (M×N independent
// SpscRings) with the FOUND-A22 2D auto-permission-tree:
//
//   * M Producer slots   — each LINEAR (one owner per producer index).
//                           Permission<Producer<UserTag, I>> for I in [0, M).
//   * N Consumer slots   — each LINEAR (one owner per consumer index).
//                           Permission<Consumer<UserTag, J>> for J in [0, N).
//
// All permissions are derived from a single Whole<UserTag> root via
// the FOUND-A22 split_grid factory:
//
//   auto whole = permission_root_mint<grid_tag::Whole<UserTag>>();
//   auto grid  = split_grid<grid_tag::Whole<UserTag>, M, N>(whole);
//   //                                                ^^ M+N tokens
//
// Each handle is STATICALLY INDEXED at compile time — ProducerHandle<I>
// knows at type-level that it serves shard I, and try_push() takes no
// id parameter.  Two distinct slot indices yield distinct handle types,
// so the type system enforces "this thread serves exactly one slot"
// structurally.
//
//   sizeof(ProducerHandle<I>) == sizeof(Channel*)  (Permission EBO)
//   sizeof(ConsumerHandle<J>) == sizeof(Channel*)  (Permission EBO)
//
// Per-operation cost (steady state):
//   try_push(item) : ~5-8 ns  (single SpscRing acq/rel)
//   try_recv()     : ~5-8 ns × M (round-robin scan across producers
//                                  in the consumer's column)
//   producer<I>() / consumer<J>() : 0 ns (pure move semantics)
//
// ─── The cell of the channel-permission family ─────────────────────
//
//   linear × linear         = PermissionedSpscChannel    (1×1)
//   linear × fractional     = PermissionedSnapshot       (1 writer × N readers)
//   fractional × linear     = PermissionedMpscChannel    (N producers × 1 cons)
//   fractional × fractional = PermissionedMpmcChannel    (N producers × N cons)
//   linear-grid × linear-grid = PermissionedShardedGrid  (M slots × N slots)
//
// ShardedGrid is the FIFTH cell — both axes are LINEAR but
// MULTI-INSTANCE, indexed at compile time.  No Pool needed because
// each slot has a unique linear owner; the type system tracks slot
// identity via the Slice<Side<UserTag>, I> phantom index.
//
// ─── Why STATIC indexing (not runtime) ─────────────────────────────
//
// Runtime indexing (try_push(producer_id, item)) sounds flexible but
// gives up the type-system role discipline:
//
//   * Two threads holding ProducerHandle could call try_push with the
//     SAME producer_id at the same time — silent data race; no compile
//     error.
//   * One thread holding a "ProducerHandle" could pass a consumer_id
//     by mistake — silent type confusion (id is just size_t); no
//     compile error.
//
// Static indexing (try_push(item) on ProducerHandle<I>) forbids both:
//
//   * Type ProducerHandle<I> is unique per I.  The type system's
//     linearity discipline (deleted copy on the embedded Permission)
//     forbids two coexisting handles for the same slot.
//   * The handle's I is fixed at construction.  No dynamic id can be
//     supplied to try_push; the slot is structurally part of the type.
//
// ─── Constraints ────────────────────────────────────────────────────
//
//   * T satisfies SpscValue (trivially-copyable, trivially-
//     destructible).  Inherited from ShardedSpscGrid.
//   * M ≥ 1, N ≥ 1, Capacity > 0 and a power of two.
//   * Each PermissionedShardedGrid uses a distinct UserTag.  Per
//     Permission.h's grep-discoverable rule, mint each Whole<UserTag>
//     EXACTLY ONCE per program, then split via split_grid.
//   * ProducerHandle<I> and ConsumerHandle<J> are move-only via their
//     embedded Linear Permission.
//
// ─── Worked example ─────────────────────────────────────────────────
//
//   struct WorkChannel {};
//   PermissionedShardedGrid<int, 4, 3, 256, WorkChannel> grid;
//
//   // Mint root + split via FOUND-A22 generator.
//   auto whole = permission_root_mint<
//       safety::grid_whole<WorkChannel>>();
//   auto perms = safety::split_grid<
//       safety::grid_whole<WorkChannel>, 4, 3>(std::move(whole));
//
//   // Construct producers (M=4) — each statically indexed.
//   auto p0 = grid.template producer<0>(std::move(std::get<0>(perms.producers)));
//   auto p1 = grid.template producer<1>(std::move(std::get<1>(perms.producers)));
//   auto p2 = grid.template producer<2>(std::move(std::get<2>(perms.producers)));
//   auto p3 = grid.template producer<3>(std::move(std::get<3>(perms.producers)));
//
//   // Construct consumers (N=3) — each statically indexed.
//   auto c0 = grid.template consumer<0>(std::move(std::get<0>(perms.consumers)));
//   auto c1 = grid.template consumer<1>(std::move(std::get<1>(perms.consumers)));
//   auto c2 = grid.template consumer<2>(std::move(std::get<2>(perms.consumers)));
//
//   // Producers emit to round-robin-routed consumer.  Consumers
//   // round-robin across M producers in their column.
//   p0.try_push(42);              // → routed to consumer 0..2 by Routing
//   auto v = c0.try_recv();       // ← reads from any p0..p3 in column 0
//
//   // p0.try_recv()  is a COMPILE ERROR — no such method on ProducerHandle
//   // c0.try_push() is a COMPILE ERROR — no such method on ConsumerHandle
//   // grid.producer<0>(...) twice is a COMPILE ERROR — Permission consumed
//
// ─── References ─────────────────────────────────────────────────────
//
//   THREADING.md §5.5      — Tier 4 queue facade design
//   PermissionGridGenerator.h (FOUND-A22) — the 2D auto-permission tree
//   safety/PermissionTreeGenerator.h (FOUND-A21) — the 1D Slice<>
//   concurrent/ShardedGrid.h — underlying M×N SpscRing primitive
// ═══════════════════════════════════════════════════════════════════

#include <crucible/Platform.h>
#include <crucible/concurrent/ShardedGrid.h>
#include <crucible/permissions/Permission.h>
#include <crucible/safety/PermissionGridGenerator.h>
#include <crucible/safety/PermissionTreeGenerator.h>
#include <crucible/safety/Pinned.h>

#include <cstddef>
#include <optional>
#include <type_traits>
#include <utility>

namespace crucible::concurrent {

// ── Tag tree for PermissionedShardedGrid ───────────────────────────
//
// Reuses the FOUND-A22 generator's tag types DIRECTLY — Producer<I>,
// Consumer<J> aliases over Slice<ProducerSide<UserTag>, I> and
// Slice<ConsumerSide<UserTag>, J>.  No per-channel tag declaration
// needed; split_grid<grid_tag::Whole<UserTag>, M, N>(whole) yields the
// matching tuples of permissions automatically.

namespace grid_tag {

template <typename UserTag> struct Whole {};

template <typename UserTag, std::size_t I>
using Producer = safety::Producer<Whole<UserTag>, I>;

template <typename UserTag, std::size_t J>
using Consumer = safety::Consumer<Whole<UserTag>, J>;

}  // namespace grid_tag

// ── PermissionedShardedGrid<T, M, N, Capacity, UserTag, Routing> ───

template <SpscValue T,
          std::size_t M,
          std::size_t N,
          std::size_t Capacity,
          typename UserTag = void,
          typename Routing = RoundRobinRouting>
class PermissionedShardedGrid
    : public safety::Pinned<PermissionedShardedGrid<T, M, N, Capacity,
                                                    UserTag, Routing>> {
public:
    using value_type   = T;
    using user_tag     = UserTag;
    using whole_tag    = grid_tag::Whole<UserTag>;

    static constexpr std::size_t num_producers = M;
    static constexpr std::size_t num_consumers = N;
    static constexpr std::size_t shard_capacity = Capacity;

    PermissionedShardedGrid() noexcept = default;

    // ── ProducerHandle<I> ─────────────────────────────────────────
    //
    // Statically indexed by producer slot I.  Holds the linear
    // Permission<Producer<UserTag, I>> token for its slot.  Move-only
    // via Permission's deleted copy.  EXPOSES try_push only; the
    // slot index is part of the type, so try_push takes only the
    // payload.

    template <std::size_t I>
    class ProducerHandle {
        static_assert(I < M, "Producer slot index out of range");

        // Reference: channel is Pinned, address stable for life.
        // Reference forbids reassign + default-construct, and
        // implicitly deletes move-assign — same rationale as
        // PermissionedSpscChannel::ConsumerHandle.
        PermissionedShardedGrid& grid_;
        [[no_unique_address]] safety::Permission<
            grid_tag::Producer<UserTag, I>> perm_;

        constexpr ProducerHandle(PermissionedShardedGrid& g,
                                 safety::Permission<
                                     grid_tag::Producer<UserTag, I>>&& p) noexcept
            : grid_{g}, perm_{std::move(p)} {}
        friend class PermissionedShardedGrid;

    public:
        ProducerHandle(const ProducerHandle&)
            = delete("ProducerHandle owns the slot's Producer Permission — copy would duplicate the linear token, allowing two threads to share a single SPSC slot (data race on the inner SpscRing)");
        ProducerHandle& operator=(const ProducerHandle&)
            = delete("ProducerHandle owns the slot's Producer Permission — assignment would overwrite the linear token");
        constexpr ProducerHandle(ProducerHandle&&) noexcept = default;
        ProducerHandle& operator=(ProducerHandle&&)
            = delete("ProducerHandle binds to ONE shard slot for life — the slot index is part of the type");

        static constexpr std::size_t shard_index = I;

        // Push to producer slot I — Routing picks consumer column.
        // Per ShardedSpscGrid::send: ~5-8 ns uncontended.
        [[nodiscard, gnu::hot]] bool try_push(const T& item) noexcept {
            return grid_.grid_.send(I, item);
        }

        [[nodiscard]] static constexpr std::size_t capacity() noexcept {
            return Capacity;
        }
    };

    // ── ConsumerHandle<J> ─────────────────────────────────────────
    //
    // Statically indexed by consumer slot J.  Holds the linear
    // Permission<Consumer<UserTag, J>> token.  Move-only.  EXPOSES
    // try_recv only; slot index is part of the type, so try_recv
    // takes no parameters.

    template <std::size_t J>
    class ConsumerHandle {
        static_assert(J < N, "Consumer slot index out of range");

        PermissionedShardedGrid& grid_;
        [[no_unique_address]] safety::Permission<
            grid_tag::Consumer<UserTag, J>> perm_;

        constexpr ConsumerHandle(PermissionedShardedGrid& g,
                                 safety::Permission<
                                     grid_tag::Consumer<UserTag, J>>&& p) noexcept
            : grid_{g}, perm_{std::move(p)} {}
        friend class PermissionedShardedGrid;

    public:
        ConsumerHandle(const ConsumerHandle&)
            = delete("ConsumerHandle owns the slot's Consumer Permission — copy would duplicate the linear token, allowing two threads to share a single SPSC consumer slot (data race on column drain)");
        ConsumerHandle& operator=(const ConsumerHandle&)
            = delete("ConsumerHandle owns the slot's Consumer Permission — assignment would overwrite the linear token");
        constexpr ConsumerHandle(ConsumerHandle&&) noexcept = default;
        ConsumerHandle& operator=(ConsumerHandle&&)
            = delete("ConsumerHandle binds to ONE shard slot for life — the slot index is part of the type");

        static constexpr std::size_t shard_index = J;

        // Round-robin pop across all M producers in column J.
        // Per ShardedSpscGrid::try_recv.
        [[nodiscard, gnu::hot]] std::optional<T> try_recv() noexcept {
            return grid_.grid_.try_recv(J);
        }

        [[nodiscard]] static constexpr std::size_t capacity() noexcept {
            return Capacity;
        }
    };

    // ── Factories ─────────────────────────────────────────────────
    //
    // Caller obtains the M+N permission tokens via the FOUND-A22
    // split_grid<grid_tag::Whole<UserTag>, M, N>(whole) factory and
    // hands one to each producer<I>/consumer<J>.

    template <std::size_t I>
    [[nodiscard]] ProducerHandle<I> producer(
        safety::Permission<grid_tag::Producer<UserTag, I>>&& perm) noexcept
    {
        static_assert(I < M, "producer<I>(): I must be less than M");
        return ProducerHandle<I>{*this, std::move(perm)};
    }

    template <std::size_t J>
    [[nodiscard]] ConsumerHandle<J> consumer(
        safety::Permission<grid_tag::Consumer<UserTag, J>>&& perm) noexcept
    {
        static_assert(J < N, "consumer<J>(): J must be less than N");
        return ConsumerHandle<J>{*this, std::move(perm)};
    }

    // ── Diagnostics ───────────────────────────────────────────────

    [[nodiscard]] std::size_t size_approx(std::size_t producer_id,
                                          std::size_t consumer_id) const noexcept
    {
        return grid_.size_approx(producer_id, consumer_id);
    }

private:
    ShardedSpscGrid<T, M, N, Capacity, Routing> grid_;
};

}  // namespace crucible::concurrent
