#pragma once

// ═══════════════════════════════════════════════════════════════════
// Queue<T, Kind> — usage-driven compile-time routing facade
//
// One name (Queue), one set of concepts (QueueProducer / QueueConsumer
// / Stealable), one familiar shape (producer_handle() / consumer_handle()
// / try_push / try_pop), but five different lock-free primitives behind
// the curtain — picked by the Kind tag at compile time.  Zero runtime
// overhead: the chosen primitive's machine code is the ONLY code in
// the binary.
//
// ─── The dispatch table ───────────────────────────────────────────
//
//   kind::spsc<Cap>             → SpscRing<T, Cap>
//   kind::mpsc<Cap>             → MpscRing<T, Cap>
//   kind::sharded<M, N, Cap, R> → ShardedSpscGrid<T, M, N, Cap, R>
//   kind::work_stealing<Cap>    → ChaseLevDeque<T, Cap>
//
// AtomicSnapshot (the single-writer-many-reader latest-value primitive)
// is intentionally NOT in this facade — it's not a FIFO queue.  Use
// AtomicSnapshot directly, or wait for SEPLOG-F3's separate Snapshot
// facade.
//
// ─── Why a facade at all ───────────────────────────────────────────
//
// Crucible's concurrent primitives have justifiably-different APIs:
//   - SpscRing:        try_push(item) / try_pop()
//   - MpscRing:        try_push(item) / try_pop() (single consumer)
//   - ShardedSpscGrid: send(producer_id, item) / try_recv(consumer_id)
//   - ChaseLevDeque:   push_bottom / pop_bottom (owner) + steal_top (thieves)
//
// Each shape reflects the underlying algorithm — sharded needs an index,
// work-stealing has asymmetric owner/thief access.  The facade does NOT
// flatten the differences (that would lose information).  Instead it
// gives each Kind its own typed handle (ProducerHandle, ConsumerHandle,
// OwnerHandle, ThiefHandle) returned by named factory methods on the
// Queue.  Same naming convention across Kinds, but the right operations
// are exposed for each role.
//
// The handles are sizeof(Queue*) (or Queue*+index for sharded), trivially
// copyable, inline through to the underlying primitive.  Generated
// assembly is identical to direct primitive use — the facade is a
// transparent forwarder, not an abstraction layer.
//
// ─── Compile-time routing via WorkloadHint ─────────────────────────
//
// For users who don't want to remember which Kind to pick:
//
//   constexpr WorkloadHint hint{
//       .producer_count = 4,
//       .consumer_count = 1,
//       .capacity       = 1024,
//   };
//   auto_queue_t<Event, hint> ch;   // → Queue<Event, kind::mpsc<1024>>
//
// pick_kind<Hint>() is a consteval function that returns a Kind tag.
// auto_queue_t uses decltype(pick_kind<Hint>()) to select the Queue
// specialization.  All decisions happen at compile time; the binary
// contains exactly one primitive's worth of code.
//
// ─── Decision matrix (when picking Kind manually) ──────────────────
//
//   1 producer, 1 consumer, FIFO              → kind::spsc<Cap>
//   N producers, 1 consumer, FIFO             → kind::mpsc<Cap>
//   M producers, N consumers, per-key order   → kind::sharded<M, N, Cap, HashKeyRouting<K>>
//   M producers, N consumers, no order needed → kind::sharded<M, N, Cap, RoundRobinRouting>
//   1 owner thread + many thieves (LIFO/FIFO) → kind::work_stealing<Cap>
//   1 producer, N readers see latest only     → AtomicSnapshot directly (not a Queue)
//
// ─── The "auto-routing based on actual usage" loop ────────────────
//
// The full vision (across SEPLOG-F1/F2/F3):
//
//   1. Developer declares a Queue with a Kind based on intent (or a
//      hint).  Type system picks the optimal primitive.
//   2. (SEPLOG-F2) Permission framework wraps each handle with
//      type-encoded ownership tokens — compile-time enforcement that
//      e.g. consumer threads cannot accidentally call producer APIs.
//   3. (SEPLOG-F3) Dev-mode WorkloadProfiler observes actual usage —
//      distinct producer thread IDs, contention rate, occupancy —
//      and emits a recommended Kind ("you wrote spsc but observed
//      4 producers — use mpsc").  Developer pastes the recommended
//      Kind into source, recompiles, gets the optimal primitive.
//
// "Auto routing based on actual usage" therefore means: declare
// semantic intent (cheap), measure behavior (in dev), refine intent.
// Not runtime adaptation — that would have to migrate state and pay
// per-op dispatch cost; not worth it for queues whose ops are 5-15ns.
//
// ─── Constraints ───────────────────────────────────────────────────
//
//   * T satisfies the underlying primitive's value concept (SpscValue,
//     RingValue, DequeValue) — the Queue specialization re-declares
//     the requires-clause that the underlying primitive uses.
//   * Capacity must be a power of two and > 0 (per underlying primitive).
//   * The Queue itself is Pinned (the underlying atomics are the
//     channel identity; moving would invalidate handles).
//   * Handles are copyable in v1 — caller is responsible for ensuring
//     SPSC/work_stealing-owner singularity by thread pinning.  v2
//     (SEPLOG-F2) tightens this with Permission-typed handles.
//
// ─── Performance ───────────────────────────────────────────────────
//
//   Every Queue<T, K> method is gnu::hot + always-inline; the optimizer
//   sees through the handle to the underlying primitive's code.  No
//   indirection cost, no virtual dispatch, no variant overhead.
//   sizeof(Queue<T, K>) == sizeof(underlying primitive), verified by
//   static_assert per Kind.
// ═══════════════════════════════════════════════════════════════════

#include <crucible/Platform.h>
#include <crucible/concurrent/AtomicSnapshot.h>
#include <crucible/concurrent/ChaseLevDeque.h>
#include <crucible/concurrent/MpscRing.h>
#include <crucible/concurrent/ShardedGrid.h>
#include <crucible/concurrent/SpscRing.h>
#include <crucible/permissions/Permission.h>
#include <crucible/safety/Pinned.h>

#include <concepts>
#include <cstddef>
#include <optional>
#include <type_traits>

namespace crucible::concurrent {

// ── Kind tags ────────────────────────────────────────────────────
//
// Empty tag types selecting the underlying primitive.  Each tag's
// template parameters carry the per-primitive configuration (capacity,
// shard counts, routing policy).  Tags themselves have no state and
// no methods — they are pure type-level routing keys for the Queue's
// partial-specialization machinery.

// ── queue_tag::* — per-Queue Permission tag tree ─────────────────
//
// User picks a UserTag type to discriminate one Queue instance from
// another (so each (Whole, Producer, Consumer) triple is unique to
// one logical channel).  splits_into<Whole<U>, Producer<U>, Consumer<U>>
// is specialized true below the namespace, so permission_split works
// out-of-the-box.
//
// Why namespace-level templates rather than nested class templates:
// partial specialization of splits_into on nested member types of a
// dependent template class is not pattern-matchable by the compiler.
// Free templates here let the partial specialization deduce UserTag
// directly.

namespace queue_tag {

template <typename UserTag> struct Whole    {};
template <typename UserTag> struct Producer {};
template <typename UserTag> struct Consumer {};

}  // namespace queue_tag

namespace kind {

// kind::spsc<Cap> — single-producer, single-consumer FIFO.
// Maps to SpscRing<T, Cap>.  Cheapest available — one acquire/release
// atomic per call on isolated cache lines, no CAS.
template <std::size_t Capacity>
struct spsc {
    static constexpr std::size_t capacity = Capacity;
};

// kind::mpsc<Cap> — multiple-producers, single-consumer FIFO.
// Maps to MpscRing<T, Cap>.  Per-cell sequence protocol (Vyukov);
// producer pays one CAS, consumer is wait-free.
template <std::size_t Capacity>
struct mpsc {
    static constexpr std::size_t capacity = Capacity;
};

// kind::sharded<M, N, Cap, Routing> — M producers, N consumers,
// per-cell SPSC, no inter-shard contention.  Maps to
// ShardedSpscGrid<T, M, N, Cap, Routing>.  Use when the producer set
// and consumer set are both bounded and known at compile time.
template <std::size_t M,
          std::size_t N,
          std::size_t Capacity,
          typename Routing = RoundRobinRouting>
struct sharded {
    static constexpr std::size_t producers = M;
    static constexpr std::size_t consumers = N;
    static constexpr std::size_t capacity  = Capacity;
    using routing_type = Routing;
};

// kind::work_stealing<Cap> — one owner pushes/pops at the bottom
// (LIFO, hot cache), many thieves steal from the top (FIFO, no owner
// contention).  Maps to ChaseLevDeque<T, Cap>.  For fork-join thread
// pools / kernel compile pools.
template <std::size_t Capacity>
struct work_stealing {
    static constexpr std::size_t capacity = Capacity;
};

}  // namespace kind

// ── Type traits — Kind detection ─────────────────────────────────

template <typename K> struct is_spsc_kind          : std::false_type {};
template <std::size_t Cap> struct is_spsc_kind<kind::spsc<Cap>> : std::true_type {};

template <typename K> struct is_mpsc_kind          : std::false_type {};
template <std::size_t Cap> struct is_mpsc_kind<kind::mpsc<Cap>> : std::true_type {};

template <typename K> struct is_sharded_kind       : std::false_type {};
template <std::size_t M, std::size_t N, std::size_t Cap, typename R>
struct is_sharded_kind<kind::sharded<M, N, Cap, R>> : std::true_type {};

template <typename K> struct is_work_stealing_kind : std::false_type {};
template <std::size_t Cap> struct is_work_stealing_kind<kind::work_stealing<Cap>> : std::true_type {};

template <typename K> inline constexpr bool is_spsc_kind_v          = is_spsc_kind<K>::value;
template <typename K> inline constexpr bool is_mpsc_kind_v          = is_mpsc_kind<K>::value;
template <typename K> inline constexpr bool is_sharded_kind_v       = is_sharded_kind<K>::value;
template <typename K> inline constexpr bool is_work_stealing_kind_v = is_work_stealing_kind<K>::value;

// ── Concepts — uniform shape across Kinds ────────────────────────
//
// Generic code that wants to be Kind-agnostic uses these.  Code that
// needs Kind-specific operations (e.g. shard indexing, work stealing)
// dispatches on the more specific concepts or is_*_kind_v traits.

template <typename Handle>
concept QueueProducer = requires(Handle h, typename Handle::value_type v) {
    typename Handle::value_type;
    { h.try_push(v) } -> std::same_as<bool>;
};

template <typename Handle>
concept QueueConsumer = requires(Handle h) {
    typename Handle::value_type;
    { h.try_pop() } -> std::same_as<std::optional<typename Handle::value_type>>;
};

template <typename Handle>
concept Stealable = requires(Handle h) {
    typename Handle::value_type;
    { h.try_steal() } -> std::same_as<std::optional<typename Handle::value_type>>;
};

// ── Primary template (undefined) ─────────────────────────────────

template <typename T, typename Kind>
class Queue;

// ── Specialization: kind::spsc<Cap> → SpscRing<T, Cap> ───────────

template <SpscValue T, std::size_t Capacity>
class Queue<T, kind::spsc<Capacity>>
    : public safety::Pinned<Queue<T, kind::spsc<Capacity>>> {
public:
    using value_type = T;
    using kind_type  = kind::spsc<Capacity>;
    using impl_type  = SpscRing<T, Capacity>;

    Queue() noexcept = default;

    // ── ProducerHandle ───────────────────────────────────────────
    //
    // Sole producer endpoint.  Two construction paths:
    //   1. producer_handle()                           — convenience,
    //      no Permission.  Caller is responsible for SPSC singularity.
    //   2. producer_handle(Permission<ProducerTag>&&)  — disciplined,
    //      consumes the permission token.  Type system proves that
    //      no other handle for this tag exists in the program.
    //
    // ProducerHandle is move-only when constructed via path 2 (carries
    // the consumed Permission); copyable when constructed via path 1.
    // Both forms have sizeof(Queue*).
    class ProducerHandle {
        Queue* q_ = nullptr;
        constexpr explicit ProducerHandle(Queue& q) noexcept : q_{&q} {}
        friend class Queue;

    public:
        using value_type = T;

        [[nodiscard, gnu::hot]] bool try_push(const T& item) noexcept {
            return q_->ring_.try_push(item);
        }
    };

    // ── ConsumerHandle ───────────────────────────────────────────
    class ConsumerHandle {
        Queue* q_ = nullptr;
        constexpr explicit ConsumerHandle(Queue& q) noexcept : q_{&q} {}
        friend class Queue;

    public:
        using value_type = T;

        [[nodiscard, gnu::hot]] std::optional<T> try_pop() noexcept {
            return q_->ring_.try_pop();
        }
    };

    // ── PermissionedProducerHandle<UserTag> ──────────────────────
    //
    // Carries the Permission<Producer<UserTag>> via [[no_unique_address]]
    // (sizeof unchanged from bare handle; Permission is empty class).
    // Move-only: the embedded Permission's deleted copy ctor propagates
    // structurally.  Handle lifetime ≡ Permission lifetime: when this
    // handle destructs (e.g. exiting a permission_fork lambda), the
    // Permission destructs too, and permission_fork's parent-rebuild
    // becomes structurally sound — not "trust me bro" discipline.
    //
    // Maximum-rigor CSL encoding: the type system enforces that you
    // cannot construct a second PermissionedProducerHandle for the same
    // UserTag — the Producer Permission for that tag is uniquely held
    // by THIS handle, and it was consumed at construction.
    template <typename UserTag>
    class PermissionedProducerHandle {
        Queue* q_ = nullptr;
        [[no_unique_address]] safety::Permission<queue_tag::Producer<UserTag>> perm_;

        constexpr PermissionedProducerHandle(
            Queue& q,
            safety::Permission<queue_tag::Producer<UserTag>>&& p) noexcept
            : q_{&q}, perm_{std::move(p)} {}
        friend class Queue;

    public:
        using value_type = T;
        using user_tag   = UserTag;

        // Move-only by virtue of the Permission member.  -Werror=use-
        // after-move catches double-use; deleted copy catches accidental
        // duplication.
        PermissionedProducerHandle(const PermissionedProducerHandle&)
            = delete("PermissionedProducerHandle owns a Permission — copy would duplicate the linear token");
        PermissionedProducerHandle& operator=(const PermissionedProducerHandle&)
            = delete("PermissionedProducerHandle owns a Permission — assignment would overwrite the linear token");
        constexpr PermissionedProducerHandle(PermissionedProducerHandle&&) noexcept = default;
        constexpr PermissionedProducerHandle& operator=(PermissionedProducerHandle&&) noexcept = default;

        [[nodiscard, gnu::hot]] bool try_push(const T& item) noexcept {
            return q_->ring_.try_push(item);
        }
    };

    // ── PermissionedConsumerHandle<UserTag> ──────────────────────
    template <typename UserTag>
    class PermissionedConsumerHandle {
        Queue* q_ = nullptr;
        [[no_unique_address]] safety::Permission<queue_tag::Consumer<UserTag>> perm_;

        constexpr PermissionedConsumerHandle(
            Queue& q,
            safety::Permission<queue_tag::Consumer<UserTag>>&& c) noexcept
            : q_{&q}, perm_{std::move(c)} {}
        friend class Queue;

    public:
        using value_type = T;
        using user_tag   = UserTag;

        PermissionedConsumerHandle(const PermissionedConsumerHandle&)
            = delete("PermissionedConsumerHandle owns a Permission — copy would duplicate the linear token");
        PermissionedConsumerHandle& operator=(const PermissionedConsumerHandle&)
            = delete("PermissionedConsumerHandle owns a Permission — assignment would overwrite the linear token");
        constexpr PermissionedConsumerHandle(PermissionedConsumerHandle&&) noexcept = default;
        constexpr PermissionedConsumerHandle& operator=(PermissionedConsumerHandle&&) noexcept = default;

        [[nodiscard, gnu::hot]] std::optional<T> try_pop() noexcept {
            return q_->ring_.try_pop();
        }
    };

    // Bare factories (no Permission) — caller-responsibility discipline.
    [[nodiscard]] ProducerHandle producer_handle() noexcept { return ProducerHandle{*this}; }
    [[nodiscard]] ConsumerHandle consumer_handle() noexcept { return ConsumerHandle{*this}; }

    // Permission-typed factories — consume the Permission token; return
    // a handle that OWNS it via [[no_unique_address]].  The handle's
    // destructor releases the Permission, making permission_fork's
    // parent-rebuild structurally sound.  Pair with permission_split /
    // permission_fork; see safety/PermissionFork.h.
    template <typename UserTag>
    [[nodiscard]] PermissionedProducerHandle<UserTag> producer_handle(
        safety::Permission<queue_tag::Producer<UserTag>>&& perm) noexcept
    {
        return PermissionedProducerHandle<UserTag>{*this, std::move(perm)};
    }

    template <typename UserTag>
    [[nodiscard]] PermissionedConsumerHandle<UserTag> consumer_handle(
        safety::Permission<queue_tag::Consumer<UserTag>>&& perm) noexcept
    {
        return PermissionedConsumerHandle<UserTag>{*this, std::move(perm)};
    }

    [[nodiscard]] std::size_t        size_approx()  const noexcept { return ring_.size_approx(); }
    [[nodiscard]] bool               empty_approx() const noexcept { return ring_.empty_approx(); }
    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return Capacity; }

private:
    impl_type ring_{};
};

// ── Specialization: kind::mpsc<Cap> → MpscRing<T, Cap> ───────────

template <RingValue T, std::size_t Capacity>
class Queue<T, kind::mpsc<Capacity>>
    : public safety::Pinned<Queue<T, kind::mpsc<Capacity>>> {
public:
    using value_type = T;
    using kind_type  = kind::mpsc<Capacity>;
    using impl_type  = MpscRing<T, Capacity>;

    Queue() noexcept = default;

    // ── ProducerHandle ───────────────────────────────────────────
    //
    // Multiple producer threads MAY share a ProducerHandle (or each
    // hold their own copy — both work; underlying CAS handles it).
    // The CAS-based push pays ~12-15 ns vs SPSC's ~5 ns; that's the
    // cost of multi-producer fan-in.
    class ProducerHandle {
        Queue* q_ = nullptr;
        constexpr explicit ProducerHandle(Queue& q) noexcept : q_{&q} {}
        friend class Queue;

    public:
        using value_type = T;

        // MpscRing::try_push takes by value (consumes); facade matches.
        [[nodiscard, gnu::hot]] bool try_push(T item) noexcept {
            return q_->ring_.try_push(item);
        }
    };

    // ── ConsumerHandle ───────────────────────────────────────────
    //
    // SINGLE consumer.  Caller MUST ensure only one thread holds an
    // active ConsumerHandle.  v2 enforces via Permission<Consumer>.
    class ConsumerHandle {
        Queue* q_ = nullptr;
        constexpr explicit ConsumerHandle(Queue& q) noexcept : q_{&q} {}
        friend class Queue;

    public:
        using value_type = T;

        [[nodiscard, gnu::hot]] std::optional<T> try_pop() noexcept {
            return q_->ring_.try_pop();
        }
    };

    // PermissionedProducerHandle owns the Permission token.  For MPSC,
    // each producer thread typically gets its own Permission via
    // permission_split_n on a sharded producer tag tree.  v1 reuses
    // the SPSC-style queue_tag::Producer<UserTag>; for fan-in patterns
    // with type-distinct producers, define your own indexed sub-tags
    // and split_n into them.
    template <typename UserTag>
    class PermissionedProducerHandle {
        Queue* q_ = nullptr;
        [[no_unique_address]] safety::Permission<queue_tag::Producer<UserTag>> perm_;

        constexpr PermissionedProducerHandle(
            Queue& q,
            safety::Permission<queue_tag::Producer<UserTag>>&& p) noexcept
            : q_{&q}, perm_{std::move(p)} {}
        friend class Queue;

    public:
        using value_type = T;
        using user_tag   = UserTag;

        PermissionedProducerHandle(const PermissionedProducerHandle&)
            = delete("PermissionedProducerHandle owns a Permission — copy would duplicate the linear token");
        PermissionedProducerHandle& operator=(const PermissionedProducerHandle&)
            = delete("PermissionedProducerHandle owns a Permission — assignment would overwrite the linear token");
        constexpr PermissionedProducerHandle(PermissionedProducerHandle&&) noexcept = default;
        constexpr PermissionedProducerHandle& operator=(PermissionedProducerHandle&&) noexcept = default;

        [[nodiscard, gnu::hot]] bool try_push(T item) noexcept {
            return q_->ring_.try_push(item);
        }
    };

    template <typename UserTag>
    class PermissionedConsumerHandle {
        Queue* q_ = nullptr;
        [[no_unique_address]] safety::Permission<queue_tag::Consumer<UserTag>> perm_;

        constexpr PermissionedConsumerHandle(
            Queue& q,
            safety::Permission<queue_tag::Consumer<UserTag>>&& c) noexcept
            : q_{&q}, perm_{std::move(c)} {}
        friend class Queue;

    public:
        using value_type = T;
        using user_tag   = UserTag;

        PermissionedConsumerHandle(const PermissionedConsumerHandle&)
            = delete("PermissionedConsumerHandle owns a Permission — copy would duplicate the linear token");
        PermissionedConsumerHandle& operator=(const PermissionedConsumerHandle&)
            = delete("PermissionedConsumerHandle owns a Permission — assignment would overwrite the linear token");
        constexpr PermissionedConsumerHandle(PermissionedConsumerHandle&&) noexcept = default;
        constexpr PermissionedConsumerHandle& operator=(PermissionedConsumerHandle&&) noexcept = default;

        [[nodiscard, gnu::hot]] std::optional<T> try_pop() noexcept {
            return q_->ring_.try_pop();
        }
    };

    [[nodiscard]] ProducerHandle producer_handle() noexcept { return ProducerHandle{*this}; }
    [[nodiscard]] ConsumerHandle consumer_handle() noexcept { return ConsumerHandle{*this}; }

    template <typename UserTag>
    [[nodiscard]] PermissionedProducerHandle<UserTag> producer_handle(
        safety::Permission<queue_tag::Producer<UserTag>>&& perm) noexcept
    {
        return PermissionedProducerHandle<UserTag>{*this, std::move(perm)};
    }

    template <typename UserTag>
    [[nodiscard]] PermissionedConsumerHandle<UserTag> consumer_handle(
        safety::Permission<queue_tag::Consumer<UserTag>>&& perm) noexcept
    {
        return PermissionedConsumerHandle<UserTag>{*this, std::move(perm)};
    }

    [[nodiscard]] bool empty_approx() const noexcept { return ring_.empty_approx(); }
    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return Capacity; }

private:
    impl_type ring_{};
};

// ── Specialization: kind::sharded<M, N, Cap, R> ──────────────────

template <SpscValue T, std::size_t M, std::size_t N,
          std::size_t Capacity, typename Routing>
class Queue<T, kind::sharded<M, N, Capacity, Routing>>
    : public safety::Pinned<Queue<T, kind::sharded<M, N, Capacity, Routing>>> {
public:
    using value_type = T;
    using kind_type  = kind::sharded<M, N, Capacity, Routing>;
    using impl_type  = ShardedSpscGrid<T, M, N, Capacity, Routing>;

    Queue() noexcept = default;

    // ── ProducerHandle (bound to a specific shard at construction) ──
    //
    // Each producer thread takes ONE handle for its assigned shard
    // (typically shard = thread_index).  Calling try_push on a handle
    // pushes into that producer's row of the grid; the underlying
    // grid's Routing policy picks the destination consumer column.
    class ProducerHandle {
        Queue*      q_     = nullptr;
        std::size_t shard_ = 0;
        constexpr ProducerHandle(Queue& q, std::size_t s) noexcept : q_{&q}, shard_{s} {}
        friend class Queue;

    public:
        using value_type = T;
        static constexpr std::size_t shard_count = M;

        [[nodiscard]] std::size_t shard_id() const noexcept { return shard_; }

        [[nodiscard, gnu::hot]] bool try_push(const T& item) noexcept {
            return q_->grid_.send(shard_, item);
        }
    };

    // ── ConsumerHandle (bound to a specific shard at construction) ──
    class ConsumerHandle {
        Queue*      q_     = nullptr;
        std::size_t shard_ = 0;
        constexpr ConsumerHandle(Queue& q, std::size_t s) noexcept : q_{&q}, shard_{s} {}
        friend class Queue;

    public:
        using value_type = T;
        static constexpr std::size_t shard_count = N;

        [[nodiscard]] std::size_t shard_id() const noexcept { return shard_; }

        [[nodiscard, gnu::hot]] std::optional<T> try_pop() noexcept {
            return q_->grid_.try_recv(shard_);
        }
    };

    [[nodiscard]] ProducerHandle producer_handle(std::size_t shard) noexcept
        pre (shard < M)
    {
        return ProducerHandle{*this, shard};
    }

    [[nodiscard]] ConsumerHandle consumer_handle(std::size_t shard) noexcept
        pre (shard < N)
    {
        return ConsumerHandle{*this, shard};
    }

    [[nodiscard]] static constexpr std::size_t producer_count() noexcept { return M; }
    [[nodiscard]] static constexpr std::size_t consumer_count() noexcept { return N; }
    [[nodiscard]] static constexpr std::size_t capacity()       noexcept { return Capacity; }

private:
    impl_type grid_{};
};

// ── Specialization: kind::work_stealing<Cap> → ChaseLevDeque<T, Cap> ──

template <DequeValue T, std::size_t Capacity>
class Queue<T, kind::work_stealing<Capacity>>
    : public safety::Pinned<Queue<T, kind::work_stealing<Capacity>>> {
public:
    using value_type = T;
    using kind_type  = kind::work_stealing<Capacity>;
    using impl_type  = ChaseLevDeque<T, Capacity>;

    Queue() noexcept = default;

    // ── OwnerHandle ──────────────────────────────────────────────
    //
    // Sole owner thread.  Has both push (bottom, LIFO) and pop
    // (bottom, LIFO).  Caller MUST ensure only one thread holds an
    // OwnerHandle — work-stealing assumes a single owner.
    class OwnerHandle {
        Queue* q_ = nullptr;
        constexpr explicit OwnerHandle(Queue& q) noexcept : q_{&q} {}
        friend class Queue;

    public:
        using value_type = T;

        [[nodiscard, gnu::hot]] bool try_push(const T& item) noexcept {
            return q_->deque_.push_bottom(item);
        }

        [[nodiscard, gnu::hot]] std::optional<T> try_pop() noexcept {
            return q_->deque_.pop_bottom();
        }
    };

    // ── ThiefHandle ──────────────────────────────────────────────
    //
    // Many thief threads may hold ThiefHandles concurrently.  Each
    // try_steal grabs from the top (FIFO from the owner's perspective)
    // — no owner contention until the deque is nearly empty.
    class ThiefHandle {
        Queue* q_ = nullptr;
        constexpr explicit ThiefHandle(Queue& q) noexcept : q_{&q} {}
        friend class Queue;

    public:
        using value_type = T;

        [[nodiscard, gnu::hot]] std::optional<T> try_steal() noexcept {
            return q_->deque_.steal_top();
        }
    };

    [[nodiscard]] OwnerHandle owner_handle() noexcept { return OwnerHandle{*this}; }
    [[nodiscard]] ThiefHandle thief_handle() noexcept { return ThiefHandle{*this}; }

    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return Capacity; }

private:
    impl_type deque_{};
};

// ── WorkloadHint + consteval router ──────────────────────────────
//
// Express the workload's semantic shape once; let the compiler pick
// the optimal Kind.  The hint is a structural NTTP (non-type template
// parameter), so pick_kind<Hint>() can dispatch on its values via
// `if constexpr`.

struct WorkloadHint {
    // How many threads will concurrently push?  1 = single producer;
    // > 1 = multi-producer (MPSC if consumer_count == 1, sharded if
    // both > 1 and a per-shard mapping is feasible).
    std::size_t producer_count = 1;

    // How many threads will concurrently pop?  1 = single consumer;
    // > 1 = multi-consumer (sharded if producer_count > 1 too).
    std::size_t consumer_count = 1;

    // Capacity of each underlying ring (power-of-two; user provides).
    std::size_t capacity = 1024;

    // True iff the workload is owner+thieves (LIFO at owner side,
    // FIFO at thief side).  Implies producer_count == consumer_count == 1
    // semantically, but with thief fan-out — picks ChaseLevDeque.
    bool work_stealing = false;
};

// pick_kind<Hint>() — consteval routing function.  Returns a value
// of one of the kind::* tag types, picked from the hint via
// `if constexpr` chain.  Auto return type deduction gives each
// instantiation its specific Kind type.
//
// Routing rules:
//   work_stealing=true                          → kind::work_stealing<Cap>
//   producer_count == 1 && consumer_count == 1  → kind::spsc<Cap>
//   producer_count >  1 && consumer_count == 1  → kind::mpsc<Cap>
//   producer_count >  1 && consumer_count >  1  → kind::sharded<P, C, Cap, RoundRobinRouting>
//   producer_count == 1 && consumer_count >  1  → kind::sharded<1, C, Cap, RoundRobinRouting>
//                                                 (single producer fanning out to many
//                                                  consumers via round-robin shards)
//
// HashKeyRouting (per-key ordering) is NOT auto-routable because it
// requires a user-supplied KeyFn type — for that, instantiate
// kind::sharded<...> directly with the desired Routing.

template <WorkloadHint Hint>
[[nodiscard]] consteval auto pick_kind() noexcept {
    if constexpr (Hint.work_stealing) {
        return kind::work_stealing<Hint.capacity>{};
    } else if constexpr (Hint.producer_count == 1 && Hint.consumer_count == 1) {
        return kind::spsc<Hint.capacity>{};
    } else if constexpr (Hint.producer_count >  1 && Hint.consumer_count == 1) {
        return kind::mpsc<Hint.capacity>{};
    } else {
        // Both sides > 1 (sharded) OR single producer with many
        // consumers (also sharded with M=1).  RoundRobinRouting is
        // the safe default; users wanting per-key ordering supply
        // kind::sharded<...> directly with HashKeyRouting<KeyFn>.
        return kind::sharded<Hint.producer_count,
                             Hint.consumer_count,
                             Hint.capacity,
                             RoundRobinRouting>{};
    }
}

// auto_queue_t<T, Hint> — type alias for the routed Queue.
template <typename T, WorkloadHint Hint>
using auto_queue_t = Queue<T, decltype(pick_kind<Hint>())>;

// ── Zero-cost guarantees ──────────────────────────────────────────
//
// The facade adds NO bytes vs the underlying primitive.  Verified by
// per-Kind static_asserts.  Test harness additionally checks the
// generated assembly to confirm no extra indirection.

namespace detail {
    using SpscQueue4096 = Queue<std::uint64_t, kind::spsc<4096>>;
    using MpscQueue4096 = Queue<std::uint64_t, kind::mpsc<4096>>;
    using ShardedQueue22 = Queue<std::uint64_t, kind::sharded<2, 2, 256>>;
    using WsQueue1024 = Queue<std::uint64_t, kind::work_stealing<1024>>;

    static_assert(sizeof(SpscQueue4096) == sizeof(SpscRing<std::uint64_t, 4096>),
                  "Queue<T, spsc<N>> must be exactly its underlying SpscRing");
    static_assert(sizeof(MpscQueue4096) == sizeof(MpscRing<std::uint64_t, 4096>),
                  "Queue<T, mpsc<N>> must be exactly its underlying MpscRing");
    static_assert(sizeof(ShardedQueue22) == sizeof(ShardedSpscGrid<std::uint64_t, 2, 2, 256>),
                  "Queue<T, sharded<...>> must be exactly its underlying ShardedSpscGrid");
    static_assert(sizeof(WsQueue1024) == sizeof(ChaseLevDeque<std::uint64_t, 1024>),
                  "Queue<T, work_stealing<N>> must be exactly its underlying ChaseLevDeque");

    // Handles are at most a Queue pointer + index (sharded only).
    static_assert(sizeof(SpscQueue4096::ProducerHandle) == sizeof(void*),
                  "SPSC ProducerHandle is a single pointer");
    static_assert(sizeof(MpscQueue4096::ProducerHandle) == sizeof(void*),
                  "MPSC ProducerHandle is a single pointer");
    static_assert(sizeof(ShardedQueue22::ProducerHandle) == sizeof(void*) + sizeof(std::size_t),
                  "Sharded ProducerHandle is a pointer plus shard index");
    static_assert(sizeof(WsQueue1024::OwnerHandle) == sizeof(void*),
                  "WS OwnerHandle is a single pointer");

    // pick_kind dispatch sanity.
    static_assert(std::is_same_v<
                      decltype(pick_kind<WorkloadHint{1, 1, 1024, false}>()),
                      kind::spsc<1024>>);
    static_assert(std::is_same_v<
                      decltype(pick_kind<WorkloadHint{4, 1, 1024, false}>()),
                      kind::mpsc<1024>>);
    static_assert(std::is_same_v<
                      decltype(pick_kind<WorkloadHint{4, 4, 1024, false}>()),
                      kind::sharded<4, 4, 1024, RoundRobinRouting>>);
    static_assert(std::is_same_v<
                      decltype(pick_kind<WorkloadHint{1, 1, 256, true}>()),
                      kind::work_stealing<256>>);
}  // namespace detail

}  // namespace crucible::concurrent

// ── splits_into specialization for queue_tag::* ──────────────────
//
// Declares that for any UserTag U, the triple
// (queue_tag::Whole<U>, queue_tag::Producer<U>, queue_tag::Consumer<U>)
// is a valid binary split.  Permission-typed handle factories on
// Queue<T, kind::spsc<N>> and Queue<T, kind::mpsc<N>> rely on this.
//
// The partial specialization deduces UserTag from the matched types,
// so users get the splits_into proof for free with any UserTag they
// invent — no per-tag boilerplate required at call sites.

namespace crucible::safety {

template <typename UserTag>
struct splits_into<concurrent::queue_tag::Whole<UserTag>,
                   concurrent::queue_tag::Producer<UserTag>,
                   concurrent::queue_tag::Consumer<UserTag>>
    : std::true_type {};

// And the n-ary form (used by permission_fork — it always invokes
// splits_into_pack regardless of arity).  Same Whole → (Producer,
// Consumer) split, just spelled in the variadic trait.
template <typename UserTag>
struct splits_into_pack<concurrent::queue_tag::Whole<UserTag>,
                        concurrent::queue_tag::Producer<UserTag>,
                        concurrent::queue_tag::Consumer<UserTag>>
    : std::true_type {};

}  // namespace crucible::safety
