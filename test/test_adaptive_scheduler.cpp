#include <crucible/concurrent/AdaptiveScheduler.h>

#include <algorithm>
#include <atomic>
#include <array>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <thread>
#include <vector>

namespace cc = crucible::concurrent;
namespace cs = crucible::concurrent::scheduler;

struct PriorityJobKey {
    static std::uint64_t key(cc::adaptive_detail::ticket_type ticket) noexcept {
        return ticket;
    }
};

using DeadlinePolicy = cs::Deadline<PriorityJobKey, 4, 64, 16, 1>;
using CfsPolicy = cs::Cfs<PriorityJobKey, 4, 64, 16, 1>;
using EevdfPolicy = cs::Eevdf<PriorityJobKey, 4, 64, 16, 1>;
using DeadlinePerShardPolicy = cs::DeadlinePerShard<PriorityJobKey, 4, 64, 16, 1>;
using CfsPerShardPolicy = cs::CfsPerShard<PriorityJobKey, 4, 64, 16, 1>;
using EevdfPerShardPolicy = cs::EevdfPerShard<PriorityJobKey, 4, 64, 16, 1>;

template <typename Policy>
static void run_completion_smoke(const char* name) {
    constexpr int kJobs = 1000;
    cc::Pool<Policy> pool{cc::CoreCount{2}};
    std::atomic<int> count{0};

    for (int i = 0; i < kJobs; ++i) {
        cc::dispatch(pool, [&count] {
            count.fetch_add(1, std::memory_order_relaxed);
        });
    }

    pool.wait_idle();
    if (count.load(std::memory_order_relaxed) != kJobs) {
        std::fprintf(stderr, "%s completed %d/%d jobs\n",
                     name, count.load(std::memory_order_relaxed), kJobs);
        std::abort();
    }
    if (pool.failed() != 0) {
        std::fprintf(stderr, "%s recorded job failures\n", name);
        std::abort();
    }
}

static void test_fifo_order_single_worker() {
    cc::Pool<cs::Fifo> pool{cc::CoreCount{1}};
    std::mutex mutex;
    std::vector<int> seen;
    seen.reserve(128);

    for (int i = 0; i < 128; ++i) {
        cc::dispatch(pool, [i, &mutex, &seen] {
            std::lock_guard lock{mutex};
            seen.push_back(i);
        });
    }
    pool.wait_idle();

    for (int i = 0; i < 128; ++i) {
        if (seen[static_cast<std::size_t>(i)] != i) {
            std::fprintf(stderr, "FIFO order broke at %d: got %d\n",
                         i, seen[static_cast<std::size_t>(i)]);
            std::abort();
        }
    }
}

static void test_lifo_order_single_worker() {
    cc::Pool<cs::Lifo> pool{cc::CoreCount{1}};
    std::atomic<bool> first_started{false};
    std::atomic<bool> release_first{false};
    std::mutex mutex;
    std::vector<int> seen;
    seen.reserve(17);

    cc::dispatch(pool, [&] {
        first_started.store(true, std::memory_order_release);
        while (!release_first.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        std::lock_guard lock{mutex};
        seen.push_back(0);
    });

    while (!first_started.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }

    for (int i = 1; i <= 16; ++i) {
        cc::dispatch(pool, [i, &mutex, &seen] {
            std::lock_guard lock{mutex};
            seen.push_back(i);
        });
    }
    release_first.store(true, std::memory_order_release);
    pool.wait_idle();

    if (seen.size() != 17 || seen[0] != 0) std::abort();
    for (int i = 1; i <= 16; ++i) {
        const int want = 17 - i;
        if (seen[static_cast<std::size_t>(i)] != want) {
            std::fprintf(stderr, "LIFO order broke at %d: got %d want %d\n",
                         i, seen[static_cast<std::size_t>(i)], want);
            std::abort();
        }
    }
}

static void test_priority_key_order_single_worker() {
    struct KeyedJob {
        std::uint64_t key_value = 0;
        std::atomic<bool>* first_started = nullptr;
        std::atomic<bool>* release_first = nullptr;
        std::mutex* mutex = nullptr;
        std::vector<int>* seen = nullptr;
        int value = 0;

        [[nodiscard]] std::uint64_t scheduler_key() const noexcept {
            return key_value;
        }

        void operator()() const noexcept {
            if (first_started != nullptr) {
                first_started->store(true, std::memory_order_release);
                while (!release_first->load(std::memory_order_acquire)) {
                    std::this_thread::yield();
                }
            }
            std::lock_guard lock{*mutex};
            seen->push_back(value);
        }
    };

    cc::Pool<DeadlinePolicy> pool{cc::CoreCount{1}};
    std::atomic<bool> first_started{false};
    std::atomic<bool> release_first{false};
    std::mutex mutex;
    std::vector<int> seen;
    seen.reserve(33);

    cc::dispatch(pool, KeyedJob{0, &first_started, &release_first,
                                &mutex, &seen, 0});
    while (!first_started.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }

    for (int i = 32; i >= 1; --i) {
        cc::dispatch(pool, KeyedJob{static_cast<std::uint64_t>(i),
                                    nullptr, nullptr, &mutex, &seen, i});
    }
    release_first.store(true, std::memory_order_release);
    pool.wait_idle();

    if (seen.size() != 33 || seen[0] != 0) std::abort();
    for (int i = 1; i <= 32; ++i) {
        if (seen[static_cast<std::size_t>(i)] != i) {
            std::fprintf(stderr, "priority order broke at %d: got %d\n",
                         i, seen[static_cast<std::size_t>(i)]);
            std::abort();
        }
    }
}

static void test_topology_flag() {
    cc::Pool<cs::LocalityAware> locality{cc::CoreCount{1}};
    cc::Pool<cs::Fifo> fifo{cc::CoreCount{1}};
    if (!locality.topology_consulted()) std::abort();
    if (fifo.topology_consulted()) std::abort();
}

static void test_recommended_topology_bridge() {
    static_assert(cc::Pool<cs::Fifo>::policy_queue_capacity() > 0);
    static_assert(cc::Pool<cs::Lifo>::policy_queue_capacity() > 0);
    static_assert(cc::Pool<DeadlinePolicy>::policy_queue_capacity() == 4 * 64 * 16);
    static_assert(cc::Pool<cs::Fifo>::recommended_topology<1, 1, 1024>()
                  == cc::ChannelTopology::OneToOne);
    static_assert(cc::Pool<cs::Fifo>::recommended_topology<4, 1, 1024>()
                  == cc::ChannelTopology::ManyToOne);
    static_assert(cc::Pool<cs::Fifo>::recommended_topology<8, 8, 4 * 1024 * 1024>()
                  == cc::ChannelTopology::ManyToMany);
}

static void test_workload_profile_payload_inference() {
    struct Payload {
        std::uint64_t a = 0;
        std::uint64_t b = 0;
    };

    constexpr auto profile = cc::WorkloadProfile::infer_from_payload<Payload>(7);
    static_assert(profile.budget.item_count == 7);
    static_assert(profile.budget.read_bytes == 7 * sizeof(Payload));
    static_assert(profile.budget.write_bytes == 7 * sizeof(Payload));
    static_assert(profile.recommended_parallelism == 0);
}

static void test_idle_workers_approx_tracks_running_work() {
    const std::size_t workers = std::min<std::size_t>(
        2, std::max<std::size_t>(1, cc::Topology::instance().process_cpu_count()));
    cc::Pool<cs::Fifo> pool{cc::CoreCount{workers}};
    std::atomic<std::size_t> started{0};
    std::atomic<bool> release{false};

    if (pool.idle_workers_approx() != pool.worker_count()) {
        std::abort();
    }
    if (pool.running_workers_approx() != 0) {
        std::abort();
    }

    for (std::size_t i = 0; i < workers; ++i) {
        cc::dispatch(pool, [&] {
            started.fetch_add(1, std::memory_order_release);
            while (!release.load(std::memory_order_acquire)) {
                CRUCIBLE_SPIN_PAUSE;
            }
        });
    }

    while (started.load(std::memory_order_acquire) != workers) {
        CRUCIBLE_SPIN_PAUSE;
    }

    if (pool.running_workers_approx() == 0) {
        std::abort();
    }
    if (pool.idle_workers_approx() >= pool.worker_count()) {
        std::abort();
    }

    release.store(true, std::memory_order_release);
    pool.wait_idle();

    if (pool.running_workers_approx() != 0) {
        std::abort();
    }
    if (pool.idle_workers_approx() != pool.worker_count()) {
        std::abort();
    }
}

static void test_dispatch_with_workload_l2_runs_inline() {
    cc::Pool<cs::Fifo> pool{cc::CoreCount{2}};
    const auto caller = std::this_thread::get_id();
    std::thread::id executed{};
    std::atomic<int> count{0};

    const cc::WorkloadProfile profile = cc::WorkloadProfile::from_budget(
        cc::WorkBudget{.read_bytes = 512, .write_bytes = 512, .item_count = 64});
    const auto result = cc::dispatch_with_workload(pool, profile, [&] {
        executed = std::this_thread::get_id();
        count.fetch_add(1, std::memory_order_relaxed);
    });

    if (!result.ran_inline || result.queued) std::abort();
    if (result.decision.kind != cc::ParallelismDecision::Kind::Sequential) {
        std::abort();
    }
    if (result.worker_limit != 1 || result.tasks_submitted != 1) std::abort();
    if (count.load(std::memory_order_relaxed) != 1) std::abort();
    if (executed != caller) std::abort();
    if (pool.queued_approx() != 0 || pool.pending_approx() != 0) std::abort();
    if (pool.submitted() != 1 || pool.completed() != 1 || pool.failed() != 0) {
        std::abort();
    }
}

static void test_dispatch_with_workload_dram_shards_queue() {
    const auto& topo = cc::Topology::instance();
    const std::size_t wanted_workers =
        std::min<std::size_t>(4, std::max<std::size_t>(1, topo.process_cpu_count()));
    cc::Pool<cs::LocalityAware> pool{cc::CoreCount{wanted_workers}};

    const std::size_t ws = std::max<std::size_t>(
        topo.l3_total_bytes() * 2,
        topo.l2_per_core_bytes() * wanted_workers * 4);
    const cc::WorkloadProfile profile = cc::WorkloadProfile::from_budget(
        cc::WorkBudget{
            .read_bytes = ws / 2,
            .write_bytes = ws / 2,
            .item_count = ws / sizeof(std::uint64_t),
        },
        wanted_workers,
        topo.numa_nodes() > 1 ? cc::NumaPolicy::NumaSpread
                              : cc::NumaPolicy::NumaIgnore);
    const auto expected_decision = cc::ParallelismRule::recommend(profile.budget);
    const std::size_t expected_worker_limit = std::min(
        wanted_workers,
        std::min(expected_decision.factor, profile.recommended_parallelism));

    std::array<std::atomic<int>, 4> seen{};
    std::atomic<int> total{0};
    std::mutex mutex;
    std::condition_variable cv;

    const auto result = cc::dispatch_with_workload(
        pool,
        profile,
        [&](cc::WorkShard shard) {
            if (shard.index >= seen.size()) std::abort();
            if (shard.count != expected_worker_limit) std::abort();
            seen[shard.index].fetch_add(1, std::memory_order_relaxed);
            const int after = total.fetch_add(1, std::memory_order_acq_rel) + 1;
            if (after == static_cast<int>(expected_worker_limit)) {
                std::lock_guard lock{mutex};
                cv.notify_one();
            }
        });

    if (result.decision.factor <= 1) {
        if (!result.ran_inline || result.queued) std::abort();
        return;
    }

    if (result.ran_inline || !result.queued) std::abort();
    if (!result.decision.is_parallel()) std::abort();
    if (result.worker_limit !=
        std::min(pool.worker_count(), profile.recommended_parallelism)) {
        std::abort();
    }
    if (result.tasks_submitted != result.worker_limit) std::abort();

    {
        std::unique_lock lock{mutex};
        cv.wait(lock, [&] {
            return total.load(std::memory_order_acquire) ==
                   static_cast<int>(result.tasks_submitted);
        });
    }
    pool.wait_idle();

    if (pool.completed() != result.tasks_submitted || pool.failed() != 0) {
        std::abort();
    }
    for (std::size_t i = 0; i < result.worker_limit; ++i) {
        if (seen[i].load(std::memory_order_relaxed) != 1) std::abort();
    }
}

int main() {
    run_completion_smoke<cs::Fifo>("Fifo");
    run_completion_smoke<cs::Lifo>("Lifo");
    run_completion_smoke<cs::RoundRobin>("RoundRobin");
    run_completion_smoke<cs::LocalityAware>("LocalityAware");
    run_completion_smoke<DeadlinePolicy>("Deadline");
    run_completion_smoke<CfsPolicy>("Cfs");
    run_completion_smoke<EevdfPolicy>("Eevdf");
    run_completion_smoke<DeadlinePerShardPolicy>("DeadlinePerShard");
    run_completion_smoke<CfsPerShardPolicy>("CfsPerShard");
    run_completion_smoke<EevdfPerShardPolicy>("EevdfPerShard");

    test_fifo_order_single_worker();
    test_lifo_order_single_worker();
    test_priority_key_order_single_worker();
    test_topology_flag();
    test_recommended_topology_bridge();
    test_workload_profile_payload_inference();
    test_idle_workers_approx_tracks_running_work();
    test_dispatch_with_workload_l2_runs_inline();
    test_dispatch_with_workload_dram_shards_queue();

    std::puts("test_adaptive_scheduler: all tests passed");
}
