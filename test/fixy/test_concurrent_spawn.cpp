// The two spawn mints, run for real: children that join, and shards that
// are recombined into the region they came from.
//
// The static properties of the mints live in fixy/concurrent/Spawn.h, where they
// fire wherever the types are used. What is here is the behaviour: that
// every child body ran, that the parent Permission comes back, and that
// a recombined region still describes the same storage.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/concurrent/Spawn.h>
#include <fixy/check/Check.h>
#include <fixy/os/Futex.h>
#include <fixy/os/Process.h>
#include <foundation/Linear.h>
#include <foundation/algebra/lattices/AffinityLattice.h>
#include <foundation/permissions/Permission.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <set>
#include <thread>
#include <type_traits>
#include <utility>

namespace eff = foundation::effects;
namespace perm = foundation::permissions;
namespace spawn = fixy::spawn;

namespace {

// The fork's spawning arm starts one thread per child, which is
// background work, so the context has to own Bg.
using BgCtx = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::Alloc>>;

struct Whole {
    using permission_row = eff::Row<>;
};
struct Left {
    using permission_row = eff::Row<>;
};
struct Right {
    using permission_row = eff::Row<>;
};

// The tag of the regions of the region scope door.  A region scope door mints
// its tokens.
struct RegionWhole {
    using permission_row = eff::Row<>;
    using scope_door = ::fixy::RegionScopeDoor;
};

// The tag of a region of const elements, which the arena door mints from a
// root permission.
struct ReadRegion {
    using permission_row = eff::Row<>;
};

// The allocation capability of the test context.
[[nodiscard]] eff::Alloc test_alloc() noexcept { return eff::testing::test().alloc; }

// The arena of a region of const elements: the request gets fresh elements
// that hold their positions, in a HeapArray that the arena holds for its
// life.  The arena takes one request, and a second request ends the process.
class PositionArena {
    fixy::HeapArray<int> held_{};

public:
    template <typename T>
        requires std::is_same_v<T, int const>
    [[nodiscard]] fixy::View<int const> alloc_array(eff::Alloc alloc, std::size_t count) noexcept {
        if (held_.size() != 0) [[unlikely]] {
            fixy::fatal("the arena of positions takes one request");
        }
        held_ = fixy::mint_heap_array_with<int>(
            alloc, count, [](std::size_t position) noexcept { return static_cast<int>(position); });
        return held_.view();
    }
};

}  // namespace

// The partition has to be declared where the fork can see it: splitting a
// parent into children declares nothing by itself.
namespace foundation::permissions {
template <>
struct can_split_into_pack<Whole, Left, Right> : std::true_type {};
template <>
struct has_split_pack_authoring_witness<Whole, Left, Right> : std::true_type {};
}  // namespace foundation::permissions

namespace {

// A budget past the L3 of this host, so the parallelism rule chooses the
// spawning arm.
[[nodiscard]] fixy::concurrent::WorkBudget dram_bound_budget() noexcept {
    return fixy::concurrent::WorkBudget{.read_bytes = fixy::topology::Topology::instance().l3_instance_bytes() * 2};
}

[[nodiscard]] int spawn_runs_every_child_and_returns_the_parent() {
    BgCtx ctx{eff::testing::bg()};
    auto whole = perm::mint_permission_root<Whole>();
    using WholeBrand = ::foundation::brand::brand_of_t<decltype(whole)>;

    ::fixy::Atomic<int> ran{0};

    // Each body borrows its child under the brand of the parent, and a
    // body may spell that brand.
    auto rebuilt = spawn::mint_spawn<spawn::BodyCoupling::Independent, Left, Right>(
        ctx, dram_bound_budget(), std::move(whole),
        [&ran](perm::WriteView<Left, WholeBrand> const&, BgCtx const&) noexcept {
            static_cast<void>(ran.fetch_add_acq_rel(1));
        },
        [&ran](perm::WriteView<Right, WholeBrand> const&, BgCtx const&) noexcept {
            static_cast<void>(ran.fetch_add_acq_rel(2));
        });

    // 1 from the left body and 2 from the right: a total of 3 says both
    // ran, and says which one is missing if they did not.
    if (const int total = ran.load_acquire(); total != 3) {
        ::fixy::report(::fixy::Sink::Err,
                       "mint_spawn: child bodies contributed {}, want 3 (1 = left only, 2 = right only)\n", total);
        return 1;
    }

    // The parent permission is back, under the brand it went in with,
    // which is what lets the caller keep using the region the children
    // borrowed.
    static_assert(perm::IsPermissionFor<decltype(rebuilt), Whole>, "mint_spawn must hand the parent Permission back.");
    static_assert(::foundation::brand::IsBranded<decltype(rebuilt)>,
                  "the rebuilt parent carries the brand of the parent that was consumed");
    (void)rebuilt;
    return 0;
}

// The parallelism rule chooses the arm of independent bodies from the
// budget.  A set in one core's private cache runs both bodies on the calling
// thread, and a set past L3 runs each body on a thread of its own.
[[nodiscard]] int spawn_arm_follows_the_budget() {
    BgCtx ctx{eff::testing::bg()};
    ::fixy::process::ThreadId const caller = ::fixy::process::current_thread(::fixy::BgLoadCtx{eff::testing::bg()});

    // Each body gives back the thread that ran it, beside the parent.
    auto const thread_of_body = [](auto const&, BgCtx const&) noexcept {
        return ::fixy::process::current_thread(::fixy::BgLoadCtx{eff::testing::bg()});
    };
    auto [inline_left, inline_right, after_inline] = spawn::mint_spawn<spawn::BodyCoupling::Independent, Left, Right>(
        ctx, fixy::concurrent::WorkBudget{.read_bytes = 64}, perm::mint_permission_root<Whole>(), thread_of_body,
        thread_of_body);
    (void)after_inline;
    if (inline_left != caller || inline_right != caller) {
        std::fprintf(stderr, "mint_spawn: a 64-byte budget must run both bodies on the calling thread\n");
        return 1;
    }

    auto [spawned_left, spawned_right, after_spawn] = spawn::mint_spawn<spawn::BodyCoupling::Independent, Left, Right>(
        ctx, dram_bound_budget(), perm::mint_permission_root<Whole>(), thread_of_body, thread_of_body);
    (void)after_spawn;
    if (spawned_left == caller || spawned_right == caller || spawned_left == spawned_right) {
        std::fprintf(stderr, "mint_spawn: a budget past L3 must run each body on a thread of its own\n");
        return 1;
    }
    return 0;
}

// True on the thread that runs main, and false on each worker thread.
constinit thread_local bool is_main_thread = false;

// The count of the shards of one barrier loop that started on this thread.
constinit thread_local int barrier_shards_on_this_thread = 0;

// Two bodies that exchange messages: the left body sends a ping and waits for
// the pong, and the right body waits for the ping and sends the pong.  The
// two waits end only when the two bodies run at the same time.  A body that
// runs on the spawning thread counts itself and does not wait, because a wait
// there never ends: the other body runs only after it returns.  The spawn
// gives the two bodies to the mint, and the test gives zero when no body ran
// on the spawning thread.
template <class Spawn>
[[nodiscard]] int exchange_messages(::fixy::TextView what, Spawn spawn) noexcept {
    ::fixy::Atomic<int> ping{0};
    ::fixy::Atomic<int> pong{0};
    ::fixy::Atomic<int> bodies_on_the_spawning_thread{0};
    spawn(
        [&ping, &pong, &bodies_on_the_spawning_thread](auto const&, BgCtx const&) noexcept {
            if (is_main_thread) {
                static_cast<void>(bodies_on_the_spawning_thread.fetch_add_acq_rel(1));
                return;
            }
            ping.store_release(1);
            while (pong.load_acquire() == 0) {
                CRUCIBLE_SPIN_PAUSE;
            }
        },
        [&ping, &pong, &bodies_on_the_spawning_thread](auto const&, BgCtx const&) noexcept {
            if (is_main_thread) {
                static_cast<void>(bodies_on_the_spawning_thread.fetch_add_acq_rel(1));
                return;
            }
            while (ping.load_acquire() == 0) {
                CRUCIBLE_SPIN_PAUSE;
            }
            pong.store_release(1);
        });
    if (int const count = bodies_on_the_spawning_thread.load_acquire(); count != 0) {
        ::fixy::report(::fixy::Sink::Err,
                       "{}: {} bodies that wait on each other ran one after another on the spawning thread, so the "
                       "first wait would never end\n",
                       what, count);
        return 1;
    }
    return 0;
}

// The budgets of the exchange: one in L1 of each host, one between L2 and L3
// of this host, and one past L3 of this host.
[[nodiscard]] fixy::concurrent::WorkBudget l3_resident_budget() noexcept {
    const auto& topo = fixy::topology::Topology::instance();
    return fixy::concurrent::WorkBudget{.read_bytes = (topo.l2_per_core_bytes() + topo.l3_instance_bytes()) / 2};
}

constexpr std::size_t KiB = 1024;
constexpr std::size_t MiB = 1024 * KiB;
constexpr std::size_t GiB = 1024 * MiB;

// The fake hosts of the exchange: caches so small that no budget fits, and
// caches so large that each budget fits a private cache.
constexpr fixy::concurrent::HostFacts kTinyCaches{
    .l1d_bytes = 1 * KiB, .l2_bytes = 2 * KiB, .l3_bytes = 4 * KiB, .process_cpus = 4, .largest_l3_group_cpus = 4};
constexpr fixy::concurrent::HostFacts kHugeCaches{
    .l1d_bytes = 1 * GiB, .l2_bytes = 2 * GiB, .l3_bytes = 4 * GiB, .process_cpus = 4, .largest_l3_group_cpus = 4};

// Two bodies that exchange messages end under each budget, on this host and
// on each fake host, because bodies that wait on each other get a thread
// each.  The pure arm function gives the same answer for each budget and
// each host.  An independent spawn still follows the budget and the host.
[[nodiscard]] int spawn_exchange_ends_under_each_budget() noexcept {
    BgCtx ctx{eff::testing::bg()};
    const eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test>> test_ctx{eff::testing::test()};
    int failures = 0;
    ::fixy::FixedArray<fixy::concurrent::WorkBudget, 4> const budgets{
        fixy::concurrent::WorkBudget{.read_bytes = 64}, fixy::concurrent::WorkBudget{.read_bytes = 1 * MiB},
        l3_resident_budget(), dram_bound_budget()};
    ::fixy::FixedArray<fixy::concurrent::HostFacts, 3> const hosts{kTinyCaches, kHugeCaches,
                                                                   fixy::concurrent::ParallelismRule::host_facts()};
    for (fixy::concurrent::WorkBudget const budget : budgets) {
        failures += exchange_messages(
            "mint_spawn of two bodies that exchange messages", [&ctx, budget](auto&& left, auto&& right) noexcept {
                auto rebuilt = spawn::mint_spawn<spawn::BodyCoupling::WaitOnEachOther, Left, Right>(
                    ctx, budget, perm::mint_permission_root<Whole>(), std::forward<decltype(left)>(left),
                    std::forward<decltype(right)>(right));
                ::foundation::linear::drop(std::move(rebuilt));
            });
        for (fixy::concurrent::HostFacts const& host : hosts) {
            failures +=
                exchange_messages("mint_spawn_on_host of two bodies that exchange messages",
                                  [&ctx, &test_ctx, &host, budget](auto&& left, auto&& right) noexcept {
                                      auto rebuilt =
                                          spawn::mint_spawn_on_host<spawn::BodyCoupling::WaitOnEachOther, Left, Right>(
                                              ctx, test_ctx, host, budget, perm::mint_permission_root<Whole>(),
                                              std::forward<decltype(left)>(left), std::forward<decltype(right)>(right));
                                      ::foundation::linear::drop(std::move(rebuilt));
                                  });
            if (spawn::choose_spawn_arm(spawn::BodyCoupling::WaitOnEachOther, host, budget)
                != spawn::SpawnArm::ThreadPerChild) {
                ::fixy::report(::fixy::Sink::Err, "the arm of bodies that wait on each other is not a thread each\n");
                ++failures;
            }
        }
    }

    // Independent bodies follow the host: the huge caches hold a budget of
    // 1 MiB in a private cache, and the tiny caches hold none.
    constexpr fixy::concurrent::WorkBudget one_mib{.read_bytes = 1 * MiB};
    if (spawn::choose_spawn_arm(spawn::BodyCoupling::Independent, kHugeCaches, one_mib) != spawn::SpawnArm::Inline
        || spawn::choose_spawn_arm(spawn::BodyCoupling::Independent, kTinyCaches, one_mib)
               != spawn::SpawnArm::ThreadPerChild) {
        ::fixy::report(::fixy::Sink::Err, "the arm of independent bodies does not follow the caches of the host\n");
        ++failures;
    }
    for (fixy::concurrent::HostFacts const& host :
         ::fixy::FixedArray<fixy::concurrent::HostFacts, 2>{kHugeCaches, kTinyCaches}) {
        ::fixy::Atomic<int> bodies_on_main{0};
        auto rebuilt = spawn::mint_spawn_on_host<spawn::BodyCoupling::Independent, Left, Right>(
            ctx, test_ctx, host, one_mib, perm::mint_permission_root<Whole>(),
            [&bodies_on_main](auto const&, BgCtx const&) noexcept {
                if (is_main_thread) static_cast<void>(bodies_on_main.fetch_add_acq_rel(1));
            },
            [&bodies_on_main](auto const&, BgCtx const&) noexcept {
                if (is_main_thread) static_cast<void>(bodies_on_main.fetch_add_acq_rel(1));
            });
        ::foundation::linear::drop(std::move(rebuilt));
        int const want =
            spawn::choose_spawn_arm(spawn::BodyCoupling::Independent, host, one_mib) == spawn::SpawnArm::Inline ? 2 : 0;
        if (bodies_on_main.load_acquire() != want) {
            ::fixy::report(::fixy::Sink::Err,
                           "mint_spawn_on_host ran {} independent bodies on the calling thread, want {}\n",
                           bodies_on_main.load_acquire(), want);
            ++failures;
        }
    }
    return failures;
}

// One shard per worker, each writing its own slice, then recombined. The
// sum is what says every element was visited exactly once.
[[nodiscard]] int parallel_for_visits_every_element_once() {
    constexpr std::size_t kCount = 4096;
    constexpr std::size_t kShards = 4;

    BgCtx ctx{eff::testing::bg()};

    // The body gets the ViewMut of its shard, and the shard stays in the
    // tuple of the runner, so recombine can consume it.  The region scope
    // door gives the storage back after the count of the recombined region.
    auto [whole_count, storage] =
        fixy::with_region<RegionWhole>(fixy::mint_heap_array(test_alloc(), kCount, 0), [&ctx](auto region) noexcept {
            auto whole = spawn::mint_parallel_for<spawn::BodyCoupling::Independent, kShards>(
                ctx, dram_bound_budget(), std::move(region), [](auto shard, auto) noexcept {
                    for (int& element : shard) {
                        element += 1;
                    }
                });
            std::size_t const count = whole.size();
            return fixy::Record{count, std::move(whole)};
        });

    long sum = 0;
    for (const int element : storage) {
        sum += element;
    }
    if (sum != static_cast<long>(kCount)) {
        std::fprintf(stderr,
                     "mint_parallel_for: visited sum is %ld, want %zu — a shard was skipped or "
                     "visited twice\n",
                     sum, kCount);
        return 1;
    }

    // Every element must be exactly one, not an average of one.
    std::size_t index = 0;
    for (const int element : storage) {
        if (element != 1) {
            ::fixy::report(::fixy::Sink::Err, "mint_parallel_for: element {} is {}, want 1\n", index, element);
            return 1;
        }
        ++index;
    }

    // The recombined region describes the same storage it started with.
    if (whole_count != kCount) {
        ::fixy::report(::fixy::Sink::Err, "the recombined region reports {} elements, want {}\n", whole_count, kCount);
        return 1;
    }
    return 0;
}

// N == 1 runs the body inline rather than on a thread, and must still
// recombine.
[[nodiscard]] int parallel_for_with_one_shard_runs_inline() {
    constexpr std::size_t kCount = 64;
    BgCtx ctx{eff::testing::bg()};

    auto [whole_count, storage] =
        fixy::with_region<RegionWhole>(fixy::mint_heap_array(test_alloc(), kCount, 7), [&ctx](auto region) noexcept {
            auto whole = spawn::mint_parallel_for<spawn::BodyCoupling::Independent, 1>(
                ctx, dram_bound_budget(), std::move(region), [](auto shard, auto) noexcept {
                    for (int& element : shard) {
                        element = 9;
                    }
                });
            std::size_t const count = whole.size();
            return fixy::Record{count, std::move(whole)};
        });

    std::size_t index = 0;
    for (const int element : storage) {
        if (element != 9) {
            ::fixy::report(::fixy::Sink::Err, "the single-shard body did not run: element {} is {}\n", index, element);
            return 1;
        }
        ++index;
    }
    if (whole_count != kCount) {
        ::fixy::report(::fixy::Sink::Err, "the single-shard recombine lost elements: {} of {}\n", whole_count, kCount);
        return 1;
    }
    return 0;
}

// A region of const elements lends each body the View of its elements, which
// the body reads, and the place of each shard gives the position of its first
// element in the region.  Each shard stores the sum of its elements and its
// first position in the cells of its own index, so no two shards write one
// cell.  The split cuts 4099 elements into 1025, 1025, 1025 and 1024.  The
// arena door gives the region of const elements, whose elements hold their
// positions.
[[nodiscard]] int parallel_for_of_const_elements_reads_each_shard() {
    constexpr std::size_t kCount = 4099;
    constexpr std::size_t kShards = 4;
    BgCtx ctx{eff::testing::bg()};
    PositionArena arena{};
    ::fixy::FixedArray<::fixy::Atomic<std::uint64_t>, kShards> sums{};
    ::fixy::FixedArray<::fixy::Atomic<std::uint64_t>, kShards> firsts{};
    auto whole = spawn::mint_parallel_for<spawn::BodyCoupling::Independent, kShards>(
        ctx, dram_bound_budget(),
        fixy::mint_owned_region<int const>(test_alloc(), arena, kCount, perm::mint_permission_root<ReadRegion>()),
        [&sums, &firsts](auto shard, auto place) noexcept {
            std::uint64_t sum = 0;
            for (int const element : shard) {
                sum += static_cast<std::uint64_t>(element);
            }
            std::uint64_t const first = place.first();
            sums.with_element(place.index(),
                              [sum](::fixy::Atomic<std::uint64_t>& cell) noexcept { cell.store_release(sum); });
            firsts.with_element(place.index(),
                                [first](::fixy::Atomic<std::uint64_t>& cell) noexcept { cell.store_release(first); });
        });
    ::foundation::linear::drop(std::move(whole));

    std::uint64_t first = 0;
    for (std::size_t shard = 0; shard < kShards; ++shard) {
        std::uint64_t const count = shard < kCount % kShards ? kCount / kShards + 1 : kCount / kShards;
        std::uint64_t const want_sum = count * (2 * first + count - 1) / 2;
        auto const index = ::fixy::Index<kShards>::check(shard).expect("each shard has its cells");
        std::uint64_t const got_first = firsts.with_element(
            index, [](::fixy::Atomic<std::uint64_t>& cell) noexcept { return cell.load_acquire(); });
        std::uint64_t const got_sum =
            sums.with_element(index, [](::fixy::Atomic<std::uint64_t>& cell) noexcept { return cell.load_acquire(); });
        if (got_first != first || got_sum != want_sum) {
            ::fixy::report(::fixy::Sink::Err,
                           "mint_parallel_for of const elements: shard {} starts at {} and sums {}, want {} and {}\n",
                           shard, got_first, got_sum, first, want_sum);
            return 1;
        }
        first += count;
    }
    return 0;
}

// The record of one shard: the thread that ran it and the count of its runs.
struct ShardRecord {
    std::thread::id thread{};
    int runs = 0;
};

// The thread each shard ran on, for a region with one record per shard.
// Each shard writes the records of its own part of the region, so no two
// shards share a record, and the body captures nothing.
template <std::size_t Shards>
struct ShardThreads {
    ::fixy::FixedArray<ShardRecord, Shards> records{};

    // Runs the shards under one budget and records who ran each one.  The
    // region scope door covers the run that the frame door of the records
    // lends.
    void run(fixy::concurrent::WorkBudget budget) noexcept {
        BgCtx ctx{eff::testing::bg()};
        records.with_mut([&ctx, budget](auto elements) noexcept {
            static_cast<void>(fixy::with_region<RegionWhole>(std::move(elements), [&ctx, budget](auto region) noexcept {
                return spawn::mint_parallel_for<spawn::BodyCoupling::Independent, Shards>(
                    ctx, budget, std::move(region), [](auto shard, auto) noexcept {
                        for (ShardRecord& record : shard) {
                            record.thread = std::this_thread::get_id();
                            ++record.runs;
                        }
                    });
            }));
        });
    }

    [[nodiscard]] bool every_shard_ran_once() const noexcept {
        return records.view().walk().all_of([](ShardRecord const& record) noexcept { return record.runs == 1; });
    }

    [[nodiscard]] std::size_t distinct_threads() const noexcept {
        std::set<std::thread::id> threads{};
        for (ShardRecord const& record : records) {
            threads.insert(record.thread);
        }
        return threads.size();
    }

    [[nodiscard]] bool ran_on(std::thread::id thread) const noexcept {
        return records.view().walk().any_of(
            [thread](ShardRecord const& record) noexcept { return record.thread == thread; });
    }
};

// Runs Shards shards that wait on each other as a barrier under one budget:
// each shard waits until each shard of the loop started.  The wait ends only
// when the shards run at the same time.  A shard that runs on the calling
// thread, or on a thread that ran a different shard of the loop, counts
// itself and does not wait, because its wait would never end.  The count of
// such shards is the result.  The region is empty, because the shards read
// no element.
template <std::size_t Shards>
[[nodiscard]] int run_barrier_shards(fixy::concurrent::WorkBudget budget) noexcept {
    barrier_shards_on_this_thread = 0;
    ::fixy::Atomic<std::uint32_t> arrivals{0};
    ::fixy::futex::FutexWord gate{0};
    ::fixy::Atomic<int> stuck_shards{0};
    BgCtx ctx{eff::testing::bg()};
    // The last shard opens the gate and wakes the others, which sleep on the
    // futex word of the gate.  Threads that spin on one word can starve the
    // threads that still have to arrive.  The region scope door covers an
    // empty HeapArray, which holds no storage.
    auto const barrier_body = [&arrivals, &gate, &stuck_shards](auto shard, auto) noexcept {
        static_cast<void>(shard);
        ++barrier_shards_on_this_thread;
        if (is_main_thread || barrier_shards_on_this_thread > 1) {
            static_cast<void>(stuck_shards.fetch_add_acq_rel(1));
            return;
        }
        const eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::Block>> waiter{eff::testing::bg()};
        if (arrivals.fetch_add_acq_rel(1) + 1 == Shards) {
            gate.store_release(1);
            static_cast<void>(::fixy::futex::wake_all(waiter, gate).expect("the kernel wakes the shards"));
        }
        while (gate.load_acquire() == 0) {
            static_cast<void>(::fixy::futex::wait(waiter, gate, 0).expect("a shard waits on the gate"));
        }
    };
    static_cast<void>(fixy::with_region<RegionWhole>(
        fixy::HeapArray<std::uint32_t>{}, [&ctx, budget, &barrier_body](auto region) noexcept {
            return spawn::mint_parallel_for<spawn::BodyCoupling::WaitOnEachOther, Shards>(
                ctx, budget, std::move(region), barrier_body);
        }));
    return stuck_shards.load_acquire();
}

// Runs Shards shards that wait on each other with worker options that the
// thread door refuses: the one CPU of the mask is past each CPU of the host,
// so no worker starts.  The region is empty, because the shards read no
// element.
template <std::size_t Shards>
void run_waiting_shards_with_refused_workers() noexcept {
    BgCtx ctx{eff::testing::bg()};
    const eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test>> test_ctx{eff::testing::test()};
    const fixy::thread::ThreadOptions refused_options{
        .affinity = ::foundation::algebra::lattices::AffinityMask::single(
            ::foundation::algebra::lattices::AffinityMask::kMaxCore),
    };
    static_cast<void>(fixy::with_region<RegionWhole>(
        fixy::HeapArray<std::uint32_t>{}, [&ctx, &test_ctx, &refused_options](auto region) noexcept {
            return spawn::mint_parallel_for_with_thread_options<spawn::BodyCoupling::WaitOnEachOther, Shards>(
                ctx, test_ctx, fixy::concurrent::WorkBudget{.read_bytes = 64}, std::move(region),
                [](auto shard, auto) noexcept { static_cast<void>(shard); }, refused_options);
        }));
}

// Shards that wait on each other cannot run one after another on the calling
// thread, so a thread door that starts no worker of theirs ends the process.
[[nodiscard]] int parallel_for_of_waiting_shards_dies_when_no_worker_starts() noexcept {
    const eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test>> test_ctx{eff::testing::test()};
    ::fixy::check::expect_dies(
        test_ctx, []() noexcept { run_waiting_shards_with_refused_workers<4>(); }, "no worker of the shards started");
    return 0;
}

// Shards that wait on each other end under each budget, because each shard
// gets a thread of its own.  The pure thread count gives one thread for each
// shard on each fake host, and the count of independent shards still follows
// the caches of the host.
[[nodiscard]] int parallel_for_barrier_ends_under_each_budget() noexcept {
    int failures = 0;
    ::fixy::FixedArray<fixy::concurrent::WorkBudget, 4> const budgets{
        fixy::concurrent::WorkBudget{.read_bytes = 64}, fixy::concurrent::WorkBudget{.read_bytes = 1 * MiB},
        l3_resident_budget(), dram_bound_budget()};
    ::fixy::FixedArray<fixy::concurrent::HostFacts, 3> const hosts{kTinyCaches, kHugeCaches,
                                                                   fixy::concurrent::ParallelismRule::host_facts()};
    for (fixy::concurrent::WorkBudget const budget : budgets) {
        if (int const stuck = run_barrier_shards<4>(budget); stuck != 0) {
            ::fixy::report(::fixy::Sink::Err,
                           "mint_parallel_for of four shards that wait on each other: {} shards ran on the calling "
                           "thread or after a different shard on one thread, so a wait would never end\n",
                           stuck);
            ++failures;
        }
        for (fixy::concurrent::HostFacts const& host : hosts) {
            if (spawn::choose_parallel_for_threads(spawn::BodyCoupling::WaitOnEachOther, host, budget, 4) != 4) {
                ::fixy::report(::fixy::Sink::Err, "four shards that wait on each other do not get a thread each\n");
                ++failures;
            }
        }
    }
    constexpr fixy::concurrent::WorkBudget one_mib{.read_bytes = 1 * MiB};
    if (spawn::choose_parallel_for_threads(spawn::BodyCoupling::Independent, kHugeCaches, one_mib, 4) != 1) {
        ::fixy::report(::fixy::Sink::Err, "independent shards in a private cache do not run on one thread\n");
        ++failures;
    }
    return failures;
}

// A working set in the private cache of one core is already hot there, so
// the parallelism rule keeps it on the calling thread.  A mint that
// started one thread per shard whatever the budget would fail here.
[[nodiscard]] int parallel_for_core_resident_runs_inline() {
    ShardThreads<8> record{};
    record.run(fixy::concurrent::ParallelismRule::budget_for_span<std::uint32_t>(8));
    if (!record.every_shard_ran_once()) {
        std::fprintf(stderr, "mint_parallel_for: a shard of the core-resident run did not run exactly once\n");
        return 1;
    }
    if (record.distinct_threads() != 1 || !record.ran_on(std::this_thread::get_id())) {
        std::fprintf(stderr,
                     "mint_parallel_for: a core-resident budget must run every shard on the calling thread, "
                     "but the shards ran on %zu threads\n",
                     record.distinct_threads());
        return 1;
    }
    return 0;
}

// Past L3 the rule goes parallel.  The mint starts no more threads than the
// factor of the decision, and runs each shard on exactly one thread.  A run
// whose workers started puts no shard on the calling thread.  When the
// thread door starts no worker, each shard runs on the calling thread.
[[nodiscard]] int parallel_for_dram_bound_caps_threads_at_the_factor() {
    const auto budget = dram_bound_budget();
    const auto decision = fixy::concurrent::ParallelismRule::recommend(budget);
    ShardThreads<16> record{};
    record.run(budget);
    if (!record.every_shard_ran_once()) {
        std::fprintf(stderr, "mint_parallel_for: a shard of the DRAM-bound run did not run exactly once\n");
        return 1;
    }
    const std::size_t want_threads = decision.is_parallel() ? std::min<std::size_t>(16, decision.factor) : 1;
    if (record.distinct_threads() > want_threads) {
        std::fprintf(stderr, "mint_parallel_for: the shards ran on %zu threads, but the rule allows %zu\n",
                     record.distinct_threads(), want_threads);
        return 1;
    }
    if (want_threads > 1 && record.ran_on(std::this_thread::get_id()) && record.distinct_threads() != 1) {
        std::fprintf(stderr, "mint_parallel_for: a threaded run put a shard on the calling thread\n");
        return 1;
    }
    return 0;
}

// The thread door refuses each worker of this run: the one CPU of the mask
// is past each CPU of the host, so the kernel refuses it and no worker
// starts.  The mint then runs each shard on the calling thread, and each
// shard runs one time.  Each shard holds one element, and its body adds
// one to it.
[[nodiscard]] int parallel_for_runs_inline_when_no_worker_starts() {
    constexpr std::size_t kShards = 16;
    const fixy::concurrent::WorkBudget budget = dram_bound_budget();
    const fixy::concurrent::ParallelismDecision decision = fixy::concurrent::ParallelismRule::recommend(budget);
    if (!decision.is_parallel() || decision.factor < 2) {
        ::fixy::report(::fixy::Sink::Out, "mint_parallel_for: the process has one CPU, so no run starts a worker\n");
        return 0;
    }

    BgCtx ctx{eff::testing::bg()};
    const eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test>> test_ctx{eff::testing::test()};
    const fixy::thread::ThreadOptions refused_options{
        .affinity = ::foundation::algebra::lattices::AffinityMask::single(
            ::foundation::algebra::lattices::AffinityMask::kMaxCore),
    };
    ::fixy::Atomic<bool> has_run_off_main{false};

    // The region scope door gives back the storage that the shards wrote.
    auto const storage = fixy::with_region<RegionWhole>(
        fixy::mint_heap_array(test_alloc(), kShards, std::uint32_t{0}),
        [&ctx, &test_ctx, budget, &refused_options, &has_run_off_main](auto region) noexcept {
            return spawn::mint_parallel_for_with_thread_options<spawn::BodyCoupling::Independent, kShards>(
                ctx, test_ctx, budget, std::move(region),
                [&has_run_off_main](auto shard, auto) noexcept {
                    if (!is_main_thread) has_run_off_main.store_release(true);
                    for (std::uint32_t& element : shard) {
                        ++element;
                    }
                },
                refused_options);
        });

    // The count of the shards of the region, and the first shard that did
    // not run one time.
    std::size_t shard_count = 0;
    std::size_t wrong_shard = kShards;
    std::uint32_t wrong_runs = 0;
    for (const std::uint32_t runs : storage) {
        if (runs != 1u && wrong_shard == kShards) {
            wrong_shard = shard_count;
            wrong_runs = runs;
        }
        ++shard_count;
    }

    if (has_run_off_main.load_acquire()) {
        ::fixy::report(::fixy::Sink::Err, "mint_parallel_for: a shard ran on a worker that the door refused\n");
        return 1;
    }
    if (wrong_shard != kShards) {
        ::fixy::report(::fixy::Sink::Err, "mint_parallel_for: shard {} ran {} times, want 1\n", wrong_shard,
                       wrong_runs);
        return 1;
    }
    if (shard_count != kShards) {
        ::fixy::report(::fixy::Sink::Err, "mint_parallel_for: the region holds {} shards, want {}\n", shard_count,
                       kShards);
        return 1;
    }
    return 0;
}

// Inside the shared L3 the rule caps the factor below the shard count, so
// sixteen shards share a few threads.
[[nodiscard]] int parallel_for_l3_resident_shares_threads() {
    const auto& topo = fixy::topology::Topology::instance();
    const fixy::concurrent::WorkBudget budget{.read_bytes = (topo.l2_per_core_bytes() + topo.l3_instance_bytes()) / 2};
    const auto decision = fixy::concurrent::ParallelismRule::recommend(budget);
    ShardThreads<16> record{};
    record.run(budget);
    if (!record.every_shard_ran_once()) {
        std::fprintf(stderr, "mint_parallel_for: a shard of the L3-resident run did not run exactly once\n");
        return 1;
    }
    const std::size_t want_threads = decision.is_parallel() ? std::min<std::size_t>(16, decision.factor) : 1;
    if (record.distinct_threads() > want_threads || want_threads >= 16) {
        std::fprintf(stderr,
                     "mint_parallel_for: an L3-resident run used %zu threads for 16 shards, and the rule "
                     "allows %zu\n",
                     record.distinct_threads(), want_threads);
        return 1;
    }
    return 0;
}

}  // namespace

int main() {
    is_main_thread = true;
    if (const int rc = spawn_runs_every_child_and_returns_the_parent(); rc != 0) return rc;
    if (const int rc = spawn_arm_follows_the_budget(); rc != 0) return rc;
    if (const int rc = spawn_exchange_ends_under_each_budget(); rc != 0) return rc;
    if (const int rc = parallel_for_barrier_ends_under_each_budget(); rc != 0) return rc;
    if (const int rc = parallel_for_of_waiting_shards_dies_when_no_worker_starts(); rc != 0) return rc;
    if (const int rc = parallel_for_visits_every_element_once(); rc != 0) return rc;
    if (const int rc = parallel_for_with_one_shard_runs_inline(); rc != 0) return rc;
    if (const int rc = parallel_for_of_const_elements_reads_each_shard(); rc != 0) return rc;
    if (const int rc = parallel_for_core_resident_runs_inline(); rc != 0) return rc;
    if (const int rc = parallel_for_dram_bound_caps_threads_at_the_factor(); rc != 0) return rc;
    if (const int rc = parallel_for_l3_resident_shares_threads(); rc != 0) return rc;
    if (const int rc = parallel_for_runs_inline_when_no_worker_starts(); rc != 0) return rc;
    return 0;
}
