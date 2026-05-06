#pragma once

// ── crucible::concurrent::Pool<Policy> ─────────────────────────────
//
// Policy-parametric jthread pool for Crucible background work.
//
// The shipped scheduler policy catalog already defines the queue
// topology and priority intent.  This header is the first runnable
// consumer: callers submit generic invocable jobs, the pool stores
// them in a small type-erased task table, and workers dispatch the
// table tickets through Policy::queue_template.

#include <crucible/Platform.h>
#include <crucible/concurrent/ParallelismRule.h>
#include <crucible/concurrent/SpinLock.h>
#include <crucible/concurrent/Substrate.h>
#include <crucible/concurrent/Topology.h>
#include <crucible/concurrent/scheduler/Policies.h>
#include <crucible/safety/Pinned.h>

#include <algorithm>
#include <atomic>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace crucible::concurrent {

struct CoreCount {
    std::size_t value = 0;

    [[nodiscard]] static CoreCount all_available() noexcept {
        return CoreCount{Topology::instance().process_cpu_count()};
    }
};

struct NumaNodeMask {
    std::uint64_t bits = ~std::uint64_t{0};

    [[nodiscard]] bool contains(std::size_t node) const noexcept {
        return node < 64 && ((bits >> node) & std::uint64_t{1}) != 0;
    }
};

struct WorkloadProfile {
    WorkBudget  budget{};
    std::size_t recommended_parallelism = 0;
    NumaPolicy  numa_preference = NumaPolicy::NumaIgnore;
    std::uint64_t per_job_compute_estimate = 0;

    [[nodiscard]] static constexpr WorkloadProfile
    from_budget(WorkBudget work_budget,
                std::size_t parallelism = 0,
                NumaPolicy numa = NumaPolicy::NumaIgnore,
                std::uint64_t per_job_compute = 0) noexcept {
        return WorkloadProfile{
            .budget = work_budget,
            .recommended_parallelism = parallelism,
            .numa_preference = numa,
            .per_job_compute_estimate = per_job_compute,
        };
    }

    template <typename T>
    [[nodiscard]] static constexpr WorkloadProfile
    infer_from_payload(std::size_t count = 1) noexcept {
        using value_type = std::remove_cvref_t<T>;
        return WorkloadProfile{
            .budget = ParallelismRule::budget_for_span<value_type>(count),
            .recommended_parallelism = 0,
            .numa_preference = NumaPolicy::NumaIgnore,
        };
    }
};

struct WorkShard {
    std::size_t index = 0;
    std::size_t count = 1;
    NumaPolicy  numa = NumaPolicy::NumaIgnore;
    Tier        tier = Tier::L1Resident;
};

struct DispatchWithWorkloadResult {
    ParallelismDecision decision{};
    bool        ran_inline = false;
    bool        queued = false;
    std::size_t worker_limit = 1;
    std::size_t tasks_submitted = 0;
};

namespace adaptive_detail {

struct ticket_type {
    static constexpr unsigned slot_bits = 14;
    static constexpr unsigned key_bits = 64 - slot_bits;
    static constexpr std::uint64_t max_slots = std::uint64_t{1} << slot_bits;
    static constexpr std::uint64_t key_mask =
        (std::uint64_t{1} << key_bits) - 1;

    std::uint64_t raw = 0;

    [[nodiscard]] static constexpr ticket_type
    pack(std::uint64_t key, std::size_t slot) noexcept {
        return ticket_type{
            .raw = (static_cast<std::uint64_t>(slot) << key_bits) |
                   (key & key_mask),
        };
    }

    [[nodiscard]] constexpr std::uint64_t key() const noexcept {
        return raw & key_mask;
    }

    [[nodiscard]] constexpr std::size_t slot() const noexcept {
        return raw >> key_bits;
    }

    [[nodiscard]] constexpr operator std::uint64_t() const noexcept {
        return key();
    }
};

struct ticket_key {
    static std::uint64_t key(ticket_type t) noexcept { return t.key(); }
};

template <typename Job>
concept HasSchedulerKey = requires(const Job& job) {
    { job.scheduler_key() } -> std::convertible_to<std::uint64_t>;
};

template <typename Job>
[[nodiscard]] std::uint64_t priority_key(const Job& job,
                                         std::uint64_t sequence) noexcept {
    if constexpr (HasSchedulerKey<Job>) {
        return static_cast<std::uint64_t>(job.scheduler_key());
    } else {
        return sequence;
    }
}

template <typename Policy, typename Job>
[[nodiscard]] std::uint64_t queue_key(const Job& job,
                                      std::uint64_t sequence) noexcept {
    if constexpr (Policy::priority_kind != scheduler::PriorityKind::None) {
        return priority_key(job, sequence);
    } else {
        (void)job;
        return sequence;
    }
}

template <typename Policy>
using policy_queue_t = typename Policy::template queue_template<ticket_type>;

template <std::size_t Capacity = 512,
          std::size_t Align = alignof(std::max_align_t)>
class InlineTask {
public:
    InlineTask() noexcept = default;

    template <typename Fn>
        requires (sizeof(std::decay_t<Fn>) <= Capacity)
              && (alignof(std::decay_t<Fn>) <= Align)
              && std::is_nothrow_constructible_v<std::decay_t<Fn>, Fn&&>
              && std::is_nothrow_move_constructible_v<std::decay_t<Fn>>
              && std::is_invocable_r_v<bool, std::decay_t<Fn>&>
    explicit InlineTask(Fn&& fn)
        noexcept(std::is_nothrow_constructible_v<std::decay_t<Fn>, Fn&&>)
    {
        using F = std::decay_t<Fn>;
        std::construct_at(reinterpret_cast<F*>(storage_), std::forward<Fn>(fn));
        run_ = [](void* ptr) noexcept -> bool {
            return (*std::launder(reinterpret_cast<F*>(ptr)))();
        };
        move_ = [](void* dst, void* src) noexcept {
            F* from = std::launder(reinterpret_cast<F*>(src));
            std::construct_at(reinterpret_cast<F*>(dst), std::move(*from));
            std::destroy_at(from);
        };
        destroy_ = [](void* ptr) noexcept {
            std::destroy_at(std::launder(reinterpret_cast<F*>(ptr)));
        };
    }

    InlineTask(const InlineTask&) = delete;
    InlineTask& operator=(const InlineTask&) = delete;

    InlineTask(InlineTask&& other) noexcept {
        move_from_(std::move(other));
    }

    InlineTask& operator=(InlineTask&& other) noexcept {
        if (this != &other) {
            reset_();
            move_from_(std::move(other));
        }
        return *this;
    }

    ~InlineTask() noexcept {
        reset_();
    }

    [[nodiscard]] bool operator()() noexcept {
        return run_(storage_);
    }

    [[nodiscard]] explicit operator bool() const noexcept {
        return run_ != nullptr;
    }

    [[nodiscard]] static consteval std::size_t capacity() noexcept {
        return Capacity;
    }

private:
    using RunFn = bool (*)(void*) noexcept;
    using MoveFn = void (*)(void*, void*) noexcept;
    using DestroyFn = void (*)(void*) noexcept;

    void reset_() noexcept {
        if (destroy_ != nullptr) {
            destroy_(storage_);
        }
        run_ = nullptr;
        move_ = nullptr;
        destroy_ = nullptr;
    }

    void move_from_(InlineTask&& other) noexcept {
        if (other.run_ == nullptr) return;
        other.move_(storage_, other.storage_);
        run_ = other.run_;
        move_ = other.move_;
        destroy_ = other.destroy_;
        other.run_ = nullptr;
        other.move_ = nullptr;
        other.destroy_ = nullptr;
    }

    alignas(Align) std::byte storage_[Capacity]{};
    RunFn run_ = nullptr;
    MoveFn move_ = nullptr;
    DestroyFn destroy_ = nullptr;
};

template <typename Queue>
class QueuePortal;

template <typename T, std::size_t Capacity, typename UserTag>
class QueuePortal<PermissionedMpmcChannel<T, Capacity, UserTag>> {
public:
    using queue_type = PermissionedMpmcChannel<T, Capacity, UserTag>;

    [[nodiscard]] bool try_push(const T& item) noexcept {
        auto producer = queue_.producer();
        return producer.has_value() && producer->try_push(item);
    }

    [[nodiscard]] std::optional<T> try_pop() noexcept {
        auto consumer = queue_.consumer();
        if (!consumer) return std::nullopt;
        return consumer->try_pop();
    }

    [[nodiscard]] std::size_t size_approx() const noexcept {
        return queue_.size_approx();
    }

private:
    queue_type queue_{};
};

template <typename T, std::size_t Capacity, typename UserTag>
class QueuePortal<PermissionedMpscChannel<T, Capacity, UserTag>> {
public:
    using queue_type = PermissionedMpscChannel<T, Capacity, UserTag>;

    QueuePortal()
        : consumer_{queue_.consumer(
              safety::mint_permission_root<typename queue_type::consumer_tag>())} {}

    [[nodiscard]] bool try_push(T item) noexcept {
        auto producer = queue_.producer();
        return producer.has_value() && producer->try_push(std::move(item));
    }

    [[nodiscard]] std::optional<T> try_pop() noexcept {
        return consumer_.try_pop();
    }

    [[nodiscard]] std::size_t size_approx() const noexcept {
        return queue_.size_approx();
    }

private:
    queue_type queue_{};
    typename queue_type::ConsumerHandle consumer_;
};

template <typename T, std::size_t Capacity, typename UserTag>
class QueuePortal<PermissionedChaseLevDeque<T, Capacity, UserTag>> {
public:
    using queue_type = PermissionedChaseLevDeque<T, Capacity, UserTag>;

    QueuePortal()
        : owner_{queue_.owner(
              safety::mint_permission_root<typename queue_type::owner_tag>())} {}

    [[nodiscard]] bool try_push(T item) noexcept {
        return owner_.try_push(std::move(item));
    }

    [[nodiscard]] std::optional<T> try_pop() noexcept {
        return owner_.try_pop();
    }

    [[nodiscard]] std::size_t size_approx() const noexcept {
        return queue_.size_approx();
    }

private:
    queue_type queue_{};
    typename queue_type::OwnerHandle owner_;
};

template <typename T,
          std::size_t M,
          std::size_t N,
          std::size_t Capacity,
          typename UserTag,
          typename Routing>
class QueuePortal<PermissionedShardedGrid<T, M, N, Capacity, UserTag, Routing>> {
public:
    using queue_type = PermissionedShardedGrid<T, M, N, Capacity, UserTag, Routing>;

    QueuePortal() {
        auto whole = safety::mint_permission_root<typename queue_type::whole_tag>();
        auto perms = safety::split_grid<typename queue_type::whole_tag, M, N>(
            std::move(whole));
        producer_.emplace(queue_.template producer<0>(
            std::move(std::get<0>(perms.producers))));
        emplace_consumers_(std::move(perms.consumers),
                           std::make_index_sequence<N>{});
    }

    [[nodiscard]] bool try_push(const T& item) noexcept {
        return producer_->try_push(item);
    }

    [[nodiscard]] std::optional<T> try_pop() noexcept {
        return try_pop_consumers_(std::make_index_sequence<N>{});
    }

    [[nodiscard]] std::size_t size_approx() const noexcept {
        return queue_.size_approx();
    }

private:
    template <typename Seq>
    struct consumer_tuple_for;

    template <std::size_t... Js>
    struct consumer_tuple_for<std::index_sequence<Js...>> {
        using type = std::tuple<
            typename queue_type::template ConsumerHandle<Js>...>;
    };

    using consumer_tuple_type =
        typename consumer_tuple_for<std::make_index_sequence<N>>::type;

    template <typename ConsumerPerms, std::size_t... Js>
    void emplace_consumers_(ConsumerPerms&& perms,
                            std::index_sequence<Js...>) {
        consumers_.emplace(queue_.template consumer<Js>(
            std::move(std::get<Js>(perms)))...);
    }

    template <std::size_t... Js>
    [[nodiscard]] std::optional<T>
    try_pop_consumers_(std::index_sequence<Js...>) noexcept {
        std::optional<T> result;
        ((result ? void() : void(result = std::get<Js>(*consumers_).try_pop())),
         ...);
        return result;
    }

    queue_type queue_{};
    std::optional<typename queue_type::template ProducerHandle<0>> producer_;
    std::optional<consumer_tuple_type> consumers_;
};

template <typename T,
          std::size_t M,
          std::size_t NumBuckets,
          std::size_t BucketCap,
          typename KeyExtractor,
          std::uint64_t QuantumNs,
          typename UserTag>
class QueuePortal<PermissionedCalendarGrid<T, M, NumBuckets, BucketCap,
                                           KeyExtractor, QuantumNs, UserTag>> {
public:
    using queue_type = PermissionedCalendarGrid<T, M, NumBuckets, BucketCap,
                                                KeyExtractor, QuantumNs, UserTag>;

    QueuePortal() {
        auto whole = safety::mint_permission_root<typename queue_type::whole_tag>();
        auto perms = safety::split_grid<typename queue_type::whole_tag, M, 1>(
            std::move(whole));
        producer_.emplace(queue_.template producer<0>(
            std::move(std::get<0>(perms.producers))));
        consumer_.emplace(queue_.consumer(std::move(std::get<0>(perms.consumers))));
    }

    [[nodiscard]] bool try_push(const T& item) noexcept {
        return producer_->try_push(item);
    }

    [[nodiscard]] std::optional<T> try_pop() noexcept {
        return consumer_->try_pop();
    }

    [[nodiscard]] std::size_t size_approx() const noexcept {
        return queue_.size_approx();
    }

private:
    queue_type queue_{};
    std::optional<typename queue_type::template ProducerHandle<0>> producer_;
    std::optional<typename queue_type::ConsumerHandle> consumer_;
};

template <typename T,
          std::size_t NumShards,
          std::size_t NumBuckets,
          std::size_t BucketCap,
          typename KeyExtractor,
          std::uint64_t QuantumNs,
          typename UserTag>
class QueuePortal<PermissionedShardedCalendarGrid<T, NumShards, NumBuckets,
                                                  BucketCap, KeyExtractor,
                                                  QuantumNs, UserTag>> {
public:
    using queue_type = PermissionedShardedCalendarGrid<T, NumShards, NumBuckets,
                                                       BucketCap, KeyExtractor,
                                                       QuantumNs, UserTag>;

    QueuePortal() {
        auto whole = safety::mint_permission_root<typename queue_type::whole_tag>();
        auto perms = safety::split_grid<typename queue_type::whole_tag,
                                        NumShards, NumShards>(std::move(whole));
        producer_.emplace(queue_.template producer<0>(
            std::move(std::get<0>(perms.producers))));
        consumer_.emplace(queue_.template consumer<0>(
            std::move(std::get<0>(perms.consumers))));
    }

    [[nodiscard]] bool try_push(const T& item) noexcept {
        return producer_->try_push(item);
    }

    [[nodiscard]] std::optional<T> try_pop() noexcept {
        return consumer_->try_pop();
    }

    [[nodiscard]] std::size_t size_approx() const noexcept {
        return queue_.size_approx();
    }

private:
    queue_type queue_{};
    std::optional<typename queue_type::template ProducerHandle<0>> producer_;
    std::optional<typename queue_type::template ConsumerHandle<0>> consumer_;
};

}  // namespace adaptive_detail

template <typename Policy = scheduler::DefaultPolicy>
    requires scheduler::SchedulerPolicy<Policy, adaptive_detail::ticket_type>
class Pool : public safety::Pinned<Pool<Policy>> {
public:
    using policy_type = Policy;
    using ticket_type = adaptive_detail::ticket_type;
    using policy_queue_type = adaptive_detail::policy_queue_t<Policy>;
    static constexpr std::size_t task_slot_count =
        std::min<std::size_t>(policy_queue_type::capacity(),
                              ticket_type::max_slots);

    static_assert(task_slot_count > 0,
                  "Pool<Policy> requires a non-empty policy queue");
    static_assert(task_slot_count <=
                      static_cast<std::size_t>(ticket_type::max_slots),
                  "Pool<Policy> task slot table index must fit in ticket_type");

    explicit Pool(CoreCount cores = CoreCount::all_available(),
                  NumaNodeMask numa_mask = {}) {
        const std::size_t requested = cores.value == 0 ? 1 : cores.value;
        const std::size_t available =
            std::max<std::size_t>(1, Topology::instance().process_cpu_count());
        worker_count_ = std::max<std::size_t>(1, std::min(requested, available));
        topology_consulted_ = Policy::needs_topology;

        if constexpr (Policy::needs_topology) {
            select_topology_cores_(numa_mask);
        }

        task_slots_ = std::make_unique<TaskSlot[]>(task_slot_count);
        workers_.reserve(worker_count_);
        for (std::size_t i = 0; i < worker_count_; ++i) {
            workers_.emplace_back([this, i](std::stop_token stop) {
                pin_worker_(i);
                worker_loop_(stop, i);
            });
        }
    }

    ~Pool() {
        for (auto& worker : workers_) worker.request_stop();
    }

    Pool(const Pool&)            = delete("Pool owns worker jthreads and task queue");
    Pool& operator=(const Pool&) = delete("Pool owns worker jthreads and task queue");
    Pool(Pool&&)                 = delete("Pool atomics and worker queues are identity");
    Pool& operator=(Pool&&)      = delete("Pool atomics and worker queues are identity");

    template <typename Job>
        requires std::is_invocable_r_v<void, Job&>
    void submit(Job&& job) {
        enqueue_job_(std::forward<Job>(job), worker_count_);
    }

    template <typename Job>
        requires (std::is_invocable_r_v<void, Job&> ||
                  std::is_invocable_r_v<void, Job&, WorkShard>)
    [[nodiscard]] DispatchWithWorkloadResult
    dispatch_with_workload(WorkloadProfile profile, Job&& job) {
        auto decision = ParallelismRule::recommend(profile.budget);
        if (profile.recommended_parallelism != 0) {
            decision.factor = std::min(decision.factor,
                                       profile.recommended_parallelism);
            if (decision.factor <= 1) {
                decision.kind = ParallelismDecision::Kind::Sequential;
                decision.factor = 1;
            }
        }
        if (profile.numa_preference != NumaPolicy::NumaIgnore) {
            decision.numa = profile.numa_preference;
        }
        if (decision.factor <= 1) {
            decision.kind = ParallelismDecision::Kind::Sequential;
            decision.factor = 1;
        }

        if (!decision.is_parallel()) {
            (void)invoke_inline_(std::forward<Job>(job), decision);
            return DispatchWithWorkloadResult{
                .decision = decision,
                .ran_inline = true,
                .queued = false,
                .worker_limit = 1,
                .tasks_submitted = 1,
            };
        }

        const std::size_t worker_limit = std::max<std::size_t>(
            1, std::min(decision.factor, worker_count_));
        const std::size_t submitted =
            enqueue_parallel_workload_(std::forward<Job>(job),
                                       decision,
                                       worker_limit);
        return DispatchWithWorkloadResult{
            .decision = decision,
            .ran_inline = false,
            .queued = submitted != 0,
            .worker_limit = worker_limit,
            .tasks_submitted = submitted,
        };
    }

    void wait_idle() const noexcept {
        while (completed_.load(std::memory_order_acquire) !=
               submitted_.load(std::memory_order_acquire)) {
            CRUCIBLE_SPIN_PAUSE;
        }
    }

    [[nodiscard]] std::size_t worker_count() const noexcept {
        return worker_count_;
    }

    [[nodiscard]] static constexpr std::size_t policy_queue_capacity() noexcept {
        return policy_queue_type::capacity();
    }

    [[nodiscard]] bool topology_consulted() const noexcept {
        return topology_consulted_;
    }

    [[nodiscard]] std::uint64_t submitted() const noexcept {
        return submitted_.load(std::memory_order_acquire);
    }

    [[nodiscard]] std::uint64_t completed() const noexcept {
        return completed_.load(std::memory_order_acquire);
    }

    [[nodiscard]] std::uint64_t failed() const noexcept {
        return failed_.load(std::memory_order_acquire);
    }

    [[nodiscard]] std::size_t pending_approx() const {
        std::size_t pending = 0;
        for (std::size_t i = 0; i < task_slot_count; ++i) {
            if (task_slots_[i].state.load(std::memory_order_acquire) !=
                slot_empty_state) {
                ++pending;
            }
        }
        return pending;
    }

    [[nodiscard]] std::size_t queued_approx() const noexcept {
        return queued_tickets_.load(std::memory_order_acquire);
    }

    [[nodiscard]] std::size_t running_workers_approx() const noexcept {
        return running_workers_.load(std::memory_order_acquire);
    }

    [[nodiscard]] std::size_t idle_workers_approx() const noexcept {
        const std::size_t busy = std::min(
            worker_count_,
            queued_approx() + running_workers_approx());
        return worker_count_ - busy;
    }

    [[nodiscard]] std::size_t affinity_applied_count() const noexcept {
        return affinity_applied_.load(std::memory_order_acquire);
    }

    template <std::size_t Producers,
              std::size_t Consumers,
              std::size_t WorkingSetBytes,
              bool LatestOnly = false>
    [[nodiscard]] static consteval ChannelTopology recommended_topology() {
        return recommend_topology_for_workload(
            Producers, Consumers, WorkingSetBytes, LatestOnly);
    }

private:
    struct Task {
        std::uint64_t sequence = 0;
        std::size_t worker_limit = 1;
        adaptive_detail::InlineTask<> body;
    };

    static constexpr std::uint64_t slot_state_mask = 0x3;
    static constexpr std::uint64_t slot_generation_shift = 2;
    static constexpr std::uint64_t slot_empty_state = 0;
    static constexpr std::uint64_t slot_writing_state = 1;
    static constexpr std::uint64_t slot_ready_tag = 2;
    static constexpr std::uint64_t slot_running_tag = 3;

    [[nodiscard]] static constexpr std::uint64_t
    slot_state(std::uint32_t generation, std::uint64_t tag) noexcept {
        return (static_cast<std::uint64_t>(generation)
                << slot_generation_shift) | tag;
    }

    [[nodiscard]] static constexpr std::uint64_t
    slot_ready_state(std::uint32_t generation) noexcept {
        return slot_state(generation, slot_ready_tag);
    }

    [[nodiscard]] static constexpr std::uint64_t
    slot_running_state(std::uint32_t generation) noexcept {
        return slot_state(generation, slot_running_tag);
    }

    [[nodiscard]] static constexpr bool
    slot_is_ready(std::uint64_t state) noexcept {
        return (state & slot_state_mask) == slot_ready_tag;
    }

    [[nodiscard]] static constexpr ticket_type
    pack_ticket_(std::uint64_t key, std::size_t slot_index) noexcept {
        return ticket_type::pack(key, slot_index);
    }

    [[nodiscard]] static constexpr std::size_t
    ticket_slot_(ticket_type ticket) noexcept {
        return ticket.slot();
    }

    struct alignas(64) TaskSlot {
        std::atomic<std::uint64_t> state{slot_empty_state};
        Task task{};
    };

    [[nodiscard]] TaskSlot& slot_for_(ticket_type ticket) noexcept {
        return task_slots_[ticket_slot_(ticket)];
    }

    [[nodiscard]] const TaskSlot& slot_for_(ticket_type ticket) const noexcept {
        return task_slots_[ticket_slot_(ticket)];
    }

    struct RunningWorkerGuard {
        std::atomic<std::size_t>& counter;

        explicit RunningWorkerGuard(
            std::atomic<std::size_t>& running) noexcept
            : counter{running} {
            counter.fetch_add(1, std::memory_order_acq_rel);
        }

        RunningWorkerGuard(const RunningWorkerGuard&) = delete;
        RunningWorkerGuard& operator=(const RunningWorkerGuard&) = delete;

        ~RunningWorkerGuard() noexcept {
            counter.fetch_sub(1, std::memory_order_acq_rel);
        }
    };

    template <typename Job>
    [[nodiscard]] static bool run_body_(Job& job) noexcept {
        try {
            job();
            return true;
        } catch (...) {
            return false;
        }
    }

    template <typename Job>
    [[nodiscard]] static bool run_body_(Job& job, WorkShard shard) noexcept {
        try {
            job(shard);
            return true;
        } catch (...) {
            return false;
        }
    }

    template <typename Job>
    [[nodiscard]] bool invoke_inline_(Job&& job,
                                      ParallelismDecision decision) noexcept {
        submitted_.fetch_add(1, std::memory_order_release);
        bool ok = true;
        if constexpr (std::is_invocable_r_v<void, Job&, WorkShard>) {
            const WorkShard shard{
                .index = 0,
                .count = 1,
                .numa = decision.numa,
                .tier = decision.tier,
            };
            ok = run_body_(job, shard);
        } else {
            ok = run_body_(job);
        }
        if (!ok) failed_.fetch_add(1, std::memory_order_release);
        completed_.fetch_add(1, std::memory_order_release);
        return ok;
    }

    template <typename Job>
    void enqueue_job_(Job&& job, std::size_t worker_limit) {
        const std::uint64_t sequence =
            next_sequence_.fetch_add(1, std::memory_order_relaxed);
        const std::uint64_t key =
            adaptive_detail::queue_key<Policy>(job, sequence);
        if (key > ticket_type::key_mask) [[unlikely]] std::abort();
        Task task{
            .sequence = sequence,
            .worker_limit = std::max<std::size_t>(1, worker_limit),
            .body = adaptive_detail::InlineTask<>{
                [fn = std::forward<Job>(job)]() mutable noexcept -> bool {
                    return run_body_(fn);
                }},
        };

        enqueue_task_(sequence, key, std::move(task), true);
    }

    template <typename Job>
    [[nodiscard]] std::size_t
    enqueue_parallel_workload_(Job&& job,
                               ParallelismDecision decision,
                               std::size_t worker_limit) {
        if constexpr (std::is_invocable_r_v<void, Job&, WorkShard>) {
            using job_type = std::decay_t<Job>;
            if constexpr (std::copy_constructible<job_type>) {
                job_type base{std::forward<Job>(job)};
                for (std::size_t i = 0; i < worker_limit; ++i) {
                    const WorkShard shard{
                        .index = i,
                        .count = worker_limit,
                        .numa = decision.numa,
                        .tier = decision.tier,
                    };
                    enqueue_job_([fn = base, shard]() mutable {
                        fn(shard);
                    }, worker_limit);
                }
                return worker_limit;
            } else {
                const WorkShard shard{
                    .index = 0,
                    .count = 1,
                    .numa = decision.numa,
                    .tier = decision.tier,
                };
                enqueue_job_([fn = std::forward<Job>(job), shard]() mutable {
                    fn(shard);
                }, worker_limit);
                return 1;
            }
        } else {
            enqueue_job_(std::forward<Job>(job), worker_limit);
            return 1;
        }
    }

    [[nodiscard]] bool try_enqueue_ticket_(ticket_type ticket) noexcept {
        {
            SpinGuard lock{queue_lock_};
            if (!queue_.try_push(ticket)) return false;
        }
        queued_tickets_.fetch_add(1, std::memory_order_release);
        return true;
    }

    [[nodiscard]] std::optional<ticket_type> try_pop_ticket_() noexcept {
        SpinGuard lock{queue_lock_};
        auto ticket = queue_.try_pop();
        if (ticket) {
            queued_tickets_.fetch_sub(1, std::memory_order_acq_rel);
        }
        return ticket;
    }

    [[nodiscard]] ticket_type
    publish_task_slot_(std::uint64_t sequence,
                       std::uint64_t key,
                       Task task) noexcept {
        const std::size_t slot_index = sequence % task_slot_count;
        const std::uint64_t generation64 =
            (sequence / task_slot_count) + 1;
        if (generation64 >
            std::numeric_limits<std::uint32_t>::max()) [[unlikely]] {
            std::abort();
        }

        auto& slot = task_slots_[slot_index];
        std::uint64_t expected = slot_empty_state;
        while (!slot.state.compare_exchange_weak(
            expected, slot_writing_state,
            std::memory_order_acq_rel,
            std::memory_order_acquire)) {
            expected = slot_empty_state;
            CRUCIBLE_SPIN_PAUSE;
        }

        slot.task = std::move(task);
        const auto generation = static_cast<std::uint32_t>(generation64);
        slot.state.store(slot_ready_state(generation),
                         std::memory_order_release);
        return pack_ticket_(key, slot_index);
    }

    void enqueue_task_(std::uint64_t sequence,
                       std::uint64_t key,
                       Task task,
                       bool count_submit) {
        const ticket_type ticket =
            publish_task_slot_(sequence, key, std::move(task));
        if (count_submit) {
            submitted_.fetch_add(1, std::memory_order_release);
        }

        while (!try_enqueue_ticket_(ticket)) {
            CRUCIBLE_SPIN_PAUSE;
        }

    }

    void return_ticket_(ticket_type ticket) {
        while (!try_enqueue_ticket_(ticket)) {
            CRUCIBLE_SPIN_PAUSE;
        }
    }

    [[nodiscard]] std::optional<std::size_t>
    worker_limit_for_(ticket_type ticket) const noexcept {
        const auto& slot = slot_for_(ticket);
        const std::uint64_t state =
            slot.state.load(std::memory_order_acquire);
        if (!slot_is_ready(state)) return std::nullopt;
        return slot.task.worker_limit;
    }

    [[nodiscard]] std::optional<Task> take_task_(ticket_type ticket) noexcept {
        auto& slot = slot_for_(ticket);
        std::uint64_t expected = slot.state.load(std::memory_order_acquire);
        if (!slot_is_ready(expected)) return std::nullopt;
        const std::uint64_t desired =
            (expected & ~slot_state_mask) | slot_running_tag;
        if (!slot.state.compare_exchange_strong(
            expected, desired,
            std::memory_order_acq_rel,
            std::memory_order_acquire)) {
            return std::nullopt;
        }

        Task task = std::move(slot.task);
        return task;
    }

    void release_task_slot_(ticket_type ticket) noexcept {
        auto& slot = slot_for_(ticket);
        slot.task = Task{};
        slot.state.store(slot_empty_state, std::memory_order_release);
    }

    void pin_worker_(std::size_t worker_index) noexcept {
#if CRUCIBLE_HAS_SCHED_AFFINITY
        if (!Policy::needs_topology || selected_cores_.empty()) return;
        const int cpu = selected_cores_[worker_index % selected_cores_.size()];
        if (cpu < 0 || cpu >= CPU_SETSIZE) return;
        const std::size_t cpu_index = static_cast<std::size_t>(cpu);

        cpu_set_t set;
        CPU_ZERO(&set);
        CPU_SET(cpu_index, &set);
        if (sched_setaffinity(0, sizeof(set), &set) == 0) {
            affinity_applied_.fetch_add(1, std::memory_order_release);
        }
#else
        (void)worker_index;
#endif
    }

    void worker_loop_(std::stop_token stop, std::size_t worker_index) {
        while (!stop.stop_requested()) {
            if (queued_tickets_.load(std::memory_order_acquire) == 0) {
                CRUCIBLE_SPIN_PAUSE;
                continue;
            }

            auto ticket = try_pop_ticket_();
            if (!ticket) {
                CRUCIBLE_SPIN_PAUSE;
                continue;
            }

            auto worker_limit = worker_limit_for_(*ticket);
            if (!worker_limit) continue;
            if (worker_index >= *worker_limit) {
                return_ticket_(*ticket);
                CRUCIBLE_SPIN_PAUSE;
                continue;
            }

            auto task = take_task_(*ticket);
            if (!task) continue;

            bool ok = false;
            {
                RunningWorkerGuard running{running_workers_};
                ok = task->body();
            }
            release_task_slot_(*ticket);
            if (!ok) {
                failed_.fetch_add(1, std::memory_order_release);
            }
            completed_.fetch_add(1, std::memory_order_release);
        }
    }

    void select_topology_cores_(NumaNodeMask mask) {
        selected_cores_.clear();
        const Topology& topo = Topology::instance();
        std::vector<std::size_t> candidate_nodes;
        for (std::size_t node = 0; node < topo.numa_nodes(); ++node) {
            if (mask.contains(node)) candidate_nodes.push_back(node);
        }
        std::sort(candidate_nodes.begin(), candidate_nodes.end(),
                  [&topo](std::size_t a, std::size_t b) noexcept {
                      return topo.numa_distance(0, static_cast<int>(a)) <
                             topo.numa_distance(0, static_cast<int>(b));
                  });

        for (std::size_t node : candidate_nodes) {
            for (int cpu : topo.cores_on_node(static_cast<int>(node))) {
                if (cpu >= 0) selected_cores_.push_back(cpu);
            }
        }
        if (selected_cores_.empty()) {
            for (std::size_t i = 0; i < worker_count_; ++i) {
                selected_cores_.push_back(static_cast<int>(i));
            }
        }
    }

    adaptive_detail::QueuePortal<policy_queue_type> queue_{};
    mutable SpinLock                queue_lock_;
    std::unique_ptr<TaskSlot[]>     task_slots_;
    std::vector<int>                selected_cores_;
    std::atomic<std::uint64_t>      next_sequence_{0};
    std::atomic<std::uint64_t>      submitted_{0};
    std::atomic<std::uint64_t>      completed_{0};
    std::atomic<std::uint64_t>      failed_{0};
    std::atomic<std::size_t>        queued_tickets_{0};
    std::atomic<std::size_t>        running_workers_{0};
    std::atomic<std::size_t>        affinity_applied_{0};
    std::size_t                     worker_count_ = 0;
    bool                            topology_consulted_ = false;
    // Last by declaration, first by destruction: jthread destructors
    // join while every queue, spin gate, task table, and atomic counter the
    // workers may observe is still alive.
    std::vector<std::jthread>       workers_;
};

template <typename Policy, typename Job>
    requires scheduler::SchedulerPolicy<Policy, adaptive_detail::ticket_type> &&
             std::is_invocable_r_v<void, Job&>
void dispatch(Pool<Policy>& pool, Job&& job) {
    pool.submit(std::forward<Job>(job));
}

template <typename Policy, typename Job>
    requires scheduler::SchedulerPolicy<Policy, adaptive_detail::ticket_type> &&
             (std::is_invocable_r_v<void, Job&> ||
              std::is_invocable_r_v<void, Job&, WorkShard>)
[[nodiscard]] DispatchWithWorkloadResult
dispatch_with_workload(Pool<Policy>& pool,
                       WorkloadProfile profile,
                       Job&& job) {
    return pool.dispatch_with_workload(profile, std::forward<Job>(job));
}

}  // namespace crucible::concurrent
