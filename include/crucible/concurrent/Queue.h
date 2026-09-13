#pragma once

// The Kind tag selects one lock-free primitive at compile time.  Each Kind
// keeps its own handle types rather than a single flattened interface: a
// sharded push needs a shard index and work stealing is asymmetric between
// owner and thief, and a common signature would have to hide both.
//
// A latest-value snapshot is deliberately not one of the Kinds.  It is not a
// FIFO queue and belongs behind its own name.
//
// The Kind is a declaration of intent, fixed in the source.  Switching
// primitives at run time on observed traffic would have to migrate the queued
// state and pay dispatch on every operation, which costs more than the better
// primitive returns.

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

// These are namespace-level templates rather than members of Queue because a
// partial specialization cannot pattern-match on a nested member type of a
// dependent class template.  Free templates let the splits_into specialization
// at the end of this file deduce UserTag directly.

namespace queue_tag {

template <typename UserTag>
struct Whole {};
template <typename UserTag>
struct Producer {};
template <typename UserTag>
struct Consumer {};

}  // namespace queue_tag

namespace kind {

template <std::size_t Capacity>
struct spsc {
    static constexpr std::size_t capacity = Capacity;
};

template <std::size_t Capacity>
struct mpsc {
    static constexpr std::size_t capacity = Capacity;
};

template <std::size_t M, std::size_t N, std::size_t Capacity, typename Routing = RoundRobinRouting>
struct sharded {
    static constexpr std::size_t producers = M;
    static constexpr std::size_t consumers = N;
    static constexpr std::size_t capacity = Capacity;
    using routing_type = Routing;
};

template <std::size_t Capacity>
struct work_stealing {
    static constexpr std::size_t capacity = Capacity;
};

}  // namespace kind

template <typename K>
struct is_spsc_kind : std::false_type {};
template <std::size_t Cap>
struct is_spsc_kind<kind::spsc<Cap>> : std::true_type {};

template <typename K>
struct is_mpsc_kind : std::false_type {};
template <std::size_t Cap>
struct is_mpsc_kind<kind::mpsc<Cap>> : std::true_type {};

template <typename K>
struct is_sharded_kind : std::false_type {};
template <std::size_t M, std::size_t N, std::size_t Cap, typename R>
struct is_sharded_kind<kind::sharded<M, N, Cap, R>> : std::true_type {};

template <typename K>
struct is_work_stealing_kind : std::false_type {};
template <std::size_t Cap>
struct is_work_stealing_kind<kind::work_stealing<Cap>> : std::true_type {};

template <typename K>
inline constexpr bool is_spsc_kind_v = is_spsc_kind<K>::value;
template <typename K>
inline constexpr bool is_mpsc_kind_v = is_mpsc_kind<K>::value;
template <typename K>
inline constexpr bool is_sharded_kind_v = is_sharded_kind<K>::value;
template <typename K>
inline constexpr bool is_work_stealing_kind_v = is_work_stealing_kind<K>::value;

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

template <typename T, typename Kind>
class Queue;

template <SpscValue T, std::size_t Capacity>
class Queue<T, kind::spsc<Capacity>> : public safety::Pinned<Queue<T, kind::spsc<Capacity>>> {
public:
    using value_type = T;
    using kind_type = kind::spsc<Capacity>;
    using impl_type = SpscRing<T, Capacity>;

    Queue() noexcept = default;

    // The bare handles carry no proof of uniqueness.  A second producer, or a
    // second consumer, breaks the one-thread-per-end assumption the ring is
    // built on, and nothing here catches it.  The permissioned handles below
    // consume a token instead, so a second one cannot be constructed.
    class ProducerHandle {
        Queue* q_ = nullptr;
        constexpr explicit ProducerHandle(Queue& q) noexcept : q_{&q} {}
        friend class Queue;

    public:
        using value_type = T;

        [[nodiscard, gnu::hot]] bool try_push(const T& item) noexcept { return q_->ring_.try_push(item); }
    };

    class ConsumerHandle {
        Queue* q_ = nullptr;
        constexpr explicit ConsumerHandle(Queue& q) noexcept : q_{&q} {}
        friend class Queue;

    public:
        using value_type = T;

        [[nodiscard, gnu::hot]] std::optional<T> try_pop() noexcept { return q_->ring_.try_pop(); }
    };

    // The handle owns the token it was built from, so the token dies with the
    // handle.  A scope that split a permission to hand one end away can
    // therefore recombine the halves once the handle goes out of scope.
    template <typename UserTag>
    class PermissionedProducerHandle {
        Queue* q_ = nullptr;
        [[no_unique_address]] safety::Permission<queue_tag::Producer<UserTag>> perm_;

        constexpr PermissionedProducerHandle(Queue& q, safety::Permission<queue_tag::Producer<UserTag>>&& p) noexcept
            : q_{&q}, perm_{std::move(p)} {}
        friend class Queue;

    public:
        using value_type = T;
        using user_tag = UserTag;

        PermissionedProducerHandle(const PermissionedProducerHandle&) =
            delete("PermissionedProducerHandle owns a Permission — copy would duplicate the linear token");
        PermissionedProducerHandle& operator=(const PermissionedProducerHandle&) =
            delete("PermissionedProducerHandle owns a Permission — assignment would overwrite the linear token");
        constexpr PermissionedProducerHandle(PermissionedProducerHandle&&) noexcept = default;
        constexpr PermissionedProducerHandle& operator=(PermissionedProducerHandle&&) noexcept = default;

        [[nodiscard, gnu::hot]] bool try_push(const T& item) noexcept { return q_->ring_.try_push(item); }
    };

    template <typename UserTag>
    class PermissionedConsumerHandle {
        Queue* q_ = nullptr;
        [[no_unique_address]] safety::Permission<queue_tag::Consumer<UserTag>> perm_;

        constexpr PermissionedConsumerHandle(Queue& q, safety::Permission<queue_tag::Consumer<UserTag>>&& c) noexcept
            : q_{&q}, perm_{std::move(c)} {}
        friend class Queue;

    public:
        using value_type = T;
        using user_tag = UserTag;

        PermissionedConsumerHandle(const PermissionedConsumerHandle&) =
            delete("PermissionedConsumerHandle owns a Permission — copy would duplicate the linear token");
        PermissionedConsumerHandle& operator=(const PermissionedConsumerHandle&) =
            delete("PermissionedConsumerHandle owns a Permission — assignment would overwrite the linear token");
        constexpr PermissionedConsumerHandle(PermissionedConsumerHandle&&) noexcept = default;
        constexpr PermissionedConsumerHandle& operator=(PermissionedConsumerHandle&&) noexcept = default;

        [[nodiscard, gnu::hot]] std::optional<T> try_pop() noexcept { return q_->ring_.try_pop(); }
    };

    [[nodiscard]] ProducerHandle producer_handle() noexcept { return ProducerHandle{*this}; }
    [[nodiscard]] ConsumerHandle consumer_handle() noexcept { return ConsumerHandle{*this}; }

    template <typename UserTag>
    [[nodiscard]] PermissionedProducerHandle<UserTag>
    producer_handle(safety::Permission<queue_tag::Producer<UserTag>>&& perm) noexcept {
        return PermissionedProducerHandle<UserTag>{*this, std::move(perm)};
    }

    template <typename UserTag>
    [[nodiscard]] PermissionedConsumerHandle<UserTag>
    consumer_handle(safety::Permission<queue_tag::Consumer<UserTag>>&& perm) noexcept {
        return PermissionedConsumerHandle<UserTag>{*this, std::move(perm)};
    }

    [[nodiscard]] std::size_t size_approx() const noexcept { return ring_.size_approx(); }
    [[nodiscard]] bool empty_approx() const noexcept { return ring_.empty_approx(); }
    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return Capacity; }

private:
    impl_type ring_{};
};

template <RingValue T, std::size_t Capacity>
class Queue<T, kind::mpsc<Capacity>> : public safety::Pinned<Queue<T, kind::mpsc<Capacity>>> {
public:
    using value_type = T;
    using kind_type = kind::mpsc<Capacity>;
    using impl_type = MpscRing<T, Capacity>;

    Queue() noexcept = default;

    // Producer threads may share one handle or each hold a copy.  Either way
    // the push protocol underneath is multi-producer safe.
    class ProducerHandle {
        Queue* q_ = nullptr;
        constexpr explicit ProducerHandle(Queue& q) noexcept : q_{&q} {}
        friend class Queue;

    public:
        using value_type = T;

        // By value: the push underneath consumes the item.
        [[nodiscard, gnu::hot]] bool try_push(T item) noexcept { return q_->ring_.try_push(item); }
    };

    // One consumer only.  The bare handle proves nothing, so a second
    // consuming thread breaks the ring with no diagnostic.
    class ConsumerHandle {
        Queue* q_ = nullptr;
        constexpr explicit ConsumerHandle(Queue& q) noexcept : q_{&q} {}
        friend class Queue;

    public:
        using value_type = T;

        [[nodiscard, gnu::hot]] std::optional<T> try_pop() noexcept { return q_->ring_.try_pop(); }
    };

    // There is one producer tag per user tag, so fan-in from producers that
    // must stay distinct in the type system needs a user tag per producer.
    template <typename UserTag>
    class PermissionedProducerHandle {
        Queue* q_ = nullptr;
        [[no_unique_address]] safety::Permission<queue_tag::Producer<UserTag>> perm_;

        constexpr PermissionedProducerHandle(Queue& q, safety::Permission<queue_tag::Producer<UserTag>>&& p) noexcept
            : q_{&q}, perm_{std::move(p)} {}
        friend class Queue;

    public:
        using value_type = T;
        using user_tag = UserTag;

        PermissionedProducerHandle(const PermissionedProducerHandle&) =
            delete("PermissionedProducerHandle owns a Permission — copy would duplicate the linear token");
        PermissionedProducerHandle& operator=(const PermissionedProducerHandle&) =
            delete("PermissionedProducerHandle owns a Permission — assignment would overwrite the linear token");
        constexpr PermissionedProducerHandle(PermissionedProducerHandle&&) noexcept = default;
        constexpr PermissionedProducerHandle& operator=(PermissionedProducerHandle&&) noexcept = default;

        [[nodiscard, gnu::hot]] bool try_push(T item) noexcept { return q_->ring_.try_push(item); }
    };

    template <typename UserTag>
    class PermissionedConsumerHandle {
        Queue* q_ = nullptr;
        [[no_unique_address]] safety::Permission<queue_tag::Consumer<UserTag>> perm_;

        constexpr PermissionedConsumerHandle(Queue& q, safety::Permission<queue_tag::Consumer<UserTag>>&& c) noexcept
            : q_{&q}, perm_{std::move(c)} {}
        friend class Queue;

    public:
        using value_type = T;
        using user_tag = UserTag;

        PermissionedConsumerHandle(const PermissionedConsumerHandle&) =
            delete("PermissionedConsumerHandle owns a Permission — copy would duplicate the linear token");
        PermissionedConsumerHandle& operator=(const PermissionedConsumerHandle&) =
            delete("PermissionedConsumerHandle owns a Permission — assignment would overwrite the linear token");
        constexpr PermissionedConsumerHandle(PermissionedConsumerHandle&&) noexcept = default;
        constexpr PermissionedConsumerHandle& operator=(PermissionedConsumerHandle&&) noexcept = default;

        [[nodiscard, gnu::hot]] std::optional<T> try_pop() noexcept { return q_->ring_.try_pop(); }
    };

    [[nodiscard]] ProducerHandle producer_handle() noexcept { return ProducerHandle{*this}; }
    [[nodiscard]] ConsumerHandle consumer_handle() noexcept { return ConsumerHandle{*this}; }

    template <typename UserTag>
    [[nodiscard]] PermissionedProducerHandle<UserTag>
    producer_handle(safety::Permission<queue_tag::Producer<UserTag>>&& perm) noexcept {
        return PermissionedProducerHandle<UserTag>{*this, std::move(perm)};
    }

    template <typename UserTag>
    [[nodiscard]] PermissionedConsumerHandle<UserTag>
    consumer_handle(safety::Permission<queue_tag::Consumer<UserTag>>&& perm) noexcept {
        return PermissionedConsumerHandle<UserTag>{*this, std::move(perm)};
    }

    [[nodiscard]] bool empty_approx() const noexcept { return ring_.empty_approx(); }
    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return Capacity; }

private:
    impl_type ring_{};
};

template <SpscValue T, std::size_t M, std::size_t N, std::size_t Capacity, typename Routing>
class Queue<T, kind::sharded<M, N, Capacity, Routing>>
    : public safety::Pinned<Queue<T, kind::sharded<M, N, Capacity, Routing>>> {
public:
    using value_type = T;
    using kind_type = kind::sharded<M, N, Capacity, Routing>;
    using impl_type = ShardedSpscGrid<T, M, N, Capacity, Routing>;

    Queue() noexcept = default;

    // The shard index selects this producer's own row of the grid, one row
    // per producer thread.  The routing policy picks the consumer column
    // within that row.
    class ProducerHandle {
        Queue* q_ = nullptr;
        std::size_t shard_ = 0;
        constexpr ProducerHandle(Queue& q, std::size_t s) noexcept : q_{&q}, shard_{s} {}
        friend class Queue;

    public:
        using value_type = T;
        static constexpr std::size_t shard_count = M;

        [[nodiscard]] std::size_t shard_id() const noexcept { return shard_; }

        [[nodiscard, gnu::hot]] bool try_push(const T& item) noexcept { return q_->grid_.try_push(shard_, item); }
    };

    class ConsumerHandle {
        Queue* q_ = nullptr;
        std::size_t shard_ = 0;
        constexpr ConsumerHandle(Queue& q, std::size_t s) noexcept : q_{&q}, shard_{s} {}
        friend class Queue;

    public:
        using value_type = T;
        static constexpr std::size_t shard_count = N;

        [[nodiscard]] std::size_t shard_id() const noexcept { return shard_; }

        [[nodiscard, gnu::hot]] std::optional<T> try_pop() noexcept { return q_->grid_.try_pop(shard_); }
    };

    [[nodiscard]] ProducerHandle producer_handle(std::size_t shard) noexcept pre(shard < M) {
        return ProducerHandle{*this, shard};
    }

    [[nodiscard]] ConsumerHandle consumer_handle(std::size_t shard) noexcept pre(shard < N) {
        return ConsumerHandle{*this, shard};
    }

    [[nodiscard]] static constexpr std::size_t producer_count() noexcept { return M; }
    [[nodiscard]] static constexpr std::size_t consumer_count() noexcept { return N; }
    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return Capacity; }

private:
    impl_type grid_{};
};

template <DequeValue T, std::size_t Capacity>
class Queue<T, kind::work_stealing<Capacity>> : public safety::Pinned<Queue<T, kind::work_stealing<Capacity>>> {
public:
    using value_type = T;
    using kind_type = kind::work_stealing<Capacity>;
    using impl_type = ChaseLevDeque<T, Capacity>;

    Queue() noexcept = default;

    // One owner thread only.  Both operations work the bottom end, which the
    // steal protocol assumes no other thread touches.
    class OwnerHandle {
        Queue* q_ = nullptr;
        constexpr explicit OwnerHandle(Queue& q) noexcept : q_{&q} {}
        friend class Queue;

    public:
        using value_type = T;

        [[nodiscard, gnu::hot]] bool try_push(const T& item) noexcept { return q_->deque_.push_bottom(item); }

        [[nodiscard, gnu::hot]] std::optional<T> try_pop() noexcept { return q_->deque_.pop_bottom(); }
    };

    // Any number of thieves may hold a handle at once.  Stealing works the
    // top end, so it does not contend with the owner until the deque runs
    // nearly empty and the two ends meet.
    class ThiefHandle {
        Queue* q_ = nullptr;
        constexpr explicit ThiefHandle(Queue& q) noexcept : q_{&q} {}
        friend class Queue;

    public:
        using value_type = T;

        [[nodiscard, gnu::hot]] std::optional<T> try_steal() noexcept { return q_->deque_.steal_top(); }
    };

    [[nodiscard]] OwnerHandle owner_handle() noexcept { return OwnerHandle{*this}; }
    [[nodiscard]] ThiefHandle thief_handle() noexcept { return ThiefHandle{*this}; }

    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return Capacity; }

private:
    impl_type deque_{};
};

struct WorkloadHint {
    std::size_t producer_count = 1;
    std::size_t consumer_count = 1;

    // Per underlying ring, and a power of two.
    std::size_t capacity = 1024;

    // Owner plus thieves.  It overrides the two counts, which describe a
    // symmetric producer and consumer split that work stealing does not have.
    bool work_stealing = false;
};

// Key-ordered routing is not reachable from a hint: it needs a user-supplied
// key function type, so it has to be spelled at the instantiation.

template <WorkloadHint Hint>
[[nodiscard]] consteval auto pick_kind() noexcept {
    if constexpr (Hint.work_stealing) {
        return kind::work_stealing<Hint.capacity>{};
    } else if constexpr (Hint.producer_count == 1 && Hint.consumer_count == 1) {
        return kind::spsc<Hint.capacity>{};
    } else if constexpr (Hint.producer_count > 1 && Hint.consumer_count == 1) {
        return kind::mpsc<Hint.capacity>{};
    } else {
        return kind::sharded<Hint.producer_count, Hint.consumer_count, Hint.capacity, RoundRobinRouting>{};
    }
}

template <typename T, WorkloadHint Hint>
using auto_queue_t = Queue<T, decltype(pick_kind<Hint>())>;

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

static_assert(sizeof(SpscQueue4096::ProducerHandle) == sizeof(void*), "SPSC ProducerHandle is a single pointer");
static_assert(sizeof(MpscQueue4096::ProducerHandle) == sizeof(void*), "MPSC ProducerHandle is a single pointer");
static_assert(sizeof(ShardedQueue22::ProducerHandle) == sizeof(void*) + sizeof(std::size_t),
              "Sharded ProducerHandle is a pointer plus shard index");
static_assert(sizeof(WsQueue1024::OwnerHandle) == sizeof(void*), "WS OwnerHandle is a single pointer");

static_assert(std::is_same_v<decltype(pick_kind<WorkloadHint{1, 1, 1024, false}>()), kind::spsc<1024>>);
static_assert(std::is_same_v<decltype(pick_kind<WorkloadHint{4, 1, 1024, false}>()), kind::mpsc<1024>>);
static_assert(std::is_same_v<decltype(pick_kind<WorkloadHint{4, 4, 1024, false}>()),
                             kind::sharded<4, 4, 1024, RoundRobinRouting>>);
static_assert(std::is_same_v<decltype(pick_kind<WorkloadHint{1, 1, 256, true}>()), kind::work_stealing<256>>);
}  // namespace detail

}  // namespace crucible::concurrent

// The specialization deduces UserTag from the matched types, so any user tag
// gets its split for free and no call site has to declare one.

namespace crucible::safety {

template <typename UserTag>
struct splits_into<concurrent::queue_tag::Whole<UserTag>, concurrent::queue_tag::Producer<UserTag>,
                   concurrent::queue_tag::Consumer<UserTag>> : std::true_type {};

// The same split spelled in the variadic trait.  A fork goes through the pack
// form whatever its arity, so a binary split has to be declared both ways.
template <typename UserTag>
struct splits_into_pack<concurrent::queue_tag::Whole<UserTag>, concurrent::queue_tag::Producer<UserTag>,
                        concurrent::queue_tag::Consumer<UserTag>> : std::true_type {};

template <typename UserTag>
struct splits_into_authoring_witness<concurrent::queue_tag::Whole<UserTag>, concurrent::queue_tag::Producer<UserTag>,
                                     concurrent::queue_tag::Consumer<UserTag>> : std::true_type {};

template <typename UserTag>
struct splits_into_pack_authoring_witness<concurrent::queue_tag::Whole<UserTag>,
                                          concurrent::queue_tag::Producer<UserTag>,
                                          concurrent::queue_tag::Consumer<UserTag>> : std::true_type {};

}  // namespace crucible::safety
