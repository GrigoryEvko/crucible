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
#include <crucible/concurrent/Substrate.h>
#include <crucible/concurrent/Topology.h>
#include <crucible/concurrent/scheduler/Policies.h>
#include <crucible/safety/Pinned.h>

#include <algorithm>
#include <atomic>
#include <concepts>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>
#include <tuple>
#include <type_traits>
#include <unordered_map>
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

using ticket_type = std::uint64_t;

struct ticket_key {
    static std::uint64_t key(ticket_type t) noexcept { return t; }
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
        ready_.notify_all();
    }

    Pool(const Pool&)            = delete("Pool owns worker jthreads and task queue");
    Pool& operator=(const Pool&) = delete("Pool owns worker jthreads and task queue");
    Pool(Pool&&)                 = delete("Pool atomics and condition variable are identity");
    Pool& operator=(Pool&&)      = delete("Pool atomics and condition variable are identity");

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
        std::lock_guard lock{tasks_mutex_};
        std::size_t pending = 0;
        for (const auto& [_, bucket] : tasks_) pending += bucket.size();
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
        std::function<bool()> body;
    };

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
        const std::uint64_t ticket =
            adaptive_detail::queue_key<Policy>(job, sequence);
        Task task{
            .sequence = sequence,
            .worker_limit = std::max<std::size_t>(1, worker_limit),
            .body = std::function<bool()>{
                [fn = std::forward<Job>(job)]() mutable noexcept -> bool {
                    return run_body_(fn);
                }},
        };

        enqueue_task_(ticket, std::move(task), true);
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
            std::lock_guard lock{queue_mutex_};
            if (!queue_.try_push(ticket)) return false;
        }
        queued_tickets_.fetch_add(1, std::memory_order_release);
        return true;
    }

    [[nodiscard]] std::optional<ticket_type> try_pop_ticket_() noexcept {
        std::lock_guard lock{queue_mutex_};
        auto ticket = queue_.try_pop();
        if (ticket) {
            queued_tickets_.fetch_sub(1, std::memory_order_acq_rel);
        }
        return ticket;
    }

    void enqueue_task_(ticket_type ticket, Task task, bool count_submit) {
        {
            std::lock_guard lock{tasks_mutex_};
            tasks_[ticket].push_back(std::move(task));
        }
        if (count_submit) {
            submitted_.fetch_add(1, std::memory_order_release);
        }

        while (!try_enqueue_ticket_(ticket)) {
            CRUCIBLE_SPIN_PAUSE;
            std::this_thread::yield();
        }

        ready_.notify_all();
    }

    void return_task_front_(ticket_type ticket, Task task) {
        {
            std::lock_guard lock{tasks_mutex_};
            tasks_[ticket].push_front(std::move(task));
        }

        while (!try_enqueue_ticket_(ticket)) {
            CRUCIBLE_SPIN_PAUSE;
            std::this_thread::yield();
        }
        ready_.notify_all();
    }

    [[nodiscard]] std::optional<Task> take_task_(ticket_type ticket) {
        std::lock_guard lock{tasks_mutex_};
        auto it = tasks_.find(ticket);
        if (it == tasks_.end() || it->second.empty()) return std::nullopt;

        Task task = std::move(it->second.front());
        it->second.pop_front();
        if (it->second.empty()) tasks_.erase(it);
        return task;
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
            {
                std::unique_lock lock{ready_mutex_};
                ready_.wait(lock, stop, [this] {
                    return queued_tickets_.load(std::memory_order_acquire) != 0;
                });
                if (stop.stop_requested()) break;
            }

            auto ticket = try_pop_ticket_();
            if (!ticket) continue;

            auto task = take_task_(*ticket);
            if (!task) continue;
            if (worker_index >= task->worker_limit) {
                return_task_front_(*ticket, std::move(*task));
                std::this_thread::yield();
                continue;
            }

            bool ok = false;
            {
                RunningWorkerGuard running{running_workers_};
                ok = task->body();
            }
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
    mutable std::mutex              ready_mutex_;
    mutable std::mutex              queue_mutex_;
    mutable std::mutex              tasks_mutex_;
    std::condition_variable_any     ready_;
    std::unordered_map<ticket_type, std::deque<Task>> tasks_;
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
    // join while every queue, mutex, task table, and atomic counter the
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
