// The two spawn mints, run for real: children that join, and shards that
// are recombined into the region they came from.
//
// The static properties of the mints live in fixy/os/Spawn.h, where they
// fire wherever the types are used. What is here is the behaviour: that
// every child body ran, that the parent Permission comes back, and that
// a recombined region still describes the same storage.

#include <fixy/os/Spawn.h>
#include <foundation/permissions/Permission.h>

#include <algorithm>
#include <array>
#include <atomic>
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

struct RegionWhole {
    using permission_row = eff::Row<>;
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
    return fixy::concurrent::WorkBudget{.read_bytes = fixy::concurrent::Topology::instance().l3_total_bytes() * 2};
}

[[nodiscard]] int spawn_runs_every_child_and_returns_the_parent() {
    BgCtx ctx{eff::testing::bg()};
    auto whole = perm::mint_permission_root<Whole>();
    using WholeBrand = ::foundation::brand::brand_of_t<decltype(whole)>;

    std::atomic<int> ran{0};

    // Each body borrows its child under the brand of the parent, and a
    // body may spell that brand.
    auto rebuilt = spawn::mint_spawn<Left, Right>(
        ctx, dram_bound_budget(), std::move(whole),
        [&ran](perm::WriteView<Left, WholeBrand> const&, BgCtx const&) noexcept {
            ran.fetch_add(1, std::memory_order_acq_rel);
        },
        [&ran](perm::WriteView<Right, WholeBrand> const&, BgCtx const&) noexcept {
            ran.fetch_add(2, std::memory_order_acq_rel);
        });

    // 1 from the left body and 2 from the right: a total of 3 says both
    // ran, and says which one is missing if they did not.
    if (const int total = ran.load(std::memory_order_acquire); total != 3) {
        std::fprintf(stderr, "mint_spawn: child bodies contributed %d, want 3 (1 = left only, 2 = right only)\n",
                     total);
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

// The parallelism rule chooses the arm from the budget.  A set in one
// core's private cache runs both bodies on the calling thread, and a set
// past L3 runs each body on a thread of its own.
[[nodiscard]] int spawn_arm_follows_the_budget() {
    BgCtx ctx{eff::testing::bg()};
    const std::thread::id caller = std::this_thread::get_id();

    std::array<std::thread::id, 2> inline_ids{};
    auto after_inline = spawn::mint_spawn<Left, Right>(
        ctx, fixy::concurrent::WorkBudget{.read_bytes = 64}, perm::mint_permission_root<Whole>(),
        [&inline_ids](auto const&, BgCtx const&) noexcept { inline_ids[0] = std::this_thread::get_id(); },
        [&inline_ids](auto const&, BgCtx const&) noexcept { inline_ids[1] = std::this_thread::get_id(); });
    (void)after_inline;
    if (inline_ids[0] != caller || inline_ids[1] != caller) {
        std::fprintf(stderr, "mint_spawn: a 64-byte budget must run both bodies on the calling thread\n");
        return 1;
    }

    std::array<std::thread::id, 2> spawned_ids{};
    auto after_spawn = spawn::mint_spawn<Left, Right>(
        ctx, dram_bound_budget(), perm::mint_permission_root<Whole>(),
        [&spawned_ids](auto const&, BgCtx const&) noexcept { spawned_ids[0] = std::this_thread::get_id(); },
        [&spawned_ids](auto const&, BgCtx const&) noexcept { spawned_ids[1] = std::this_thread::get_id(); });
    (void)after_spawn;
    if (spawned_ids[0] == caller || spawned_ids[1] == caller || spawned_ids[0] == spawned_ids[1]) {
        std::fprintf(stderr, "mint_spawn: a budget past L3 must run each body on a thread of its own\n");
        return 1;
    }
    return 0;
}

// One shard per worker, each writing its own slice, then recombined. The
// sum is what says every element was visited exactly once.
[[nodiscard]] int parallel_for_visits_every_element_once() {
    constexpr std::size_t kCount = 4096;
    constexpr std::size_t kShards = 4;

    BgCtx ctx{eff::testing::bg()};
    static std::array<int, kCount> storage{};
    storage.fill(0);

    auto region = fixy::OwnedRegion<int, RegionWhole>::wrap(storage.data(), kCount,
                                                            perm::mint_permission_root<RegionWhole>());

    // The body takes its shard by mutable reference, so the shard stays in
    // the tuple and recombine can consume it.
    auto whole = spawn::mint_parallel_for<kShards>(ctx, dram_bound_budget(), std::move(region), [](auto& shard) noexcept {
        for (int& element : shard) {
            element += 1;
        }
    });

    long sum = 0;
    for (const int element : storage) {
        sum += element;
    }
    if (sum != static_cast<long>(kCount)) {
        std::fprintf(stderr, "mint_parallel_for: visited sum is %ld, want %zu — a shard was skipped or "
                             "visited twice\n",
                     sum, kCount);
        return 1;
    }

    // Every element must be exactly one, not an average of one.
    for (std::size_t index = 0; index < kCount; ++index) {
        if (storage[index] != 1) {
            std::fprintf(stderr, "mint_parallel_for: element %zu is %d, want 1\n", index, storage[index]);
            return 1;
        }
    }

    // The recombined region describes the same storage it started with.
    if (whole.size() != kCount) {
        std::fprintf(stderr, "the recombined region reports %zu elements, want %zu\n", whole.size(), kCount);
        return 1;
    }
    return 0;
}

// N == 1 runs the body inline rather than on a thread, and must still
// recombine.
[[nodiscard]] int parallel_for_with_one_shard_runs_inline() {
    constexpr std::size_t kCount = 64;
    BgCtx ctx{eff::testing::bg()};
    static std::array<int, kCount> storage{};
    storage.fill(7);

    auto region = fixy::OwnedRegion<int, RegionWhole>::wrap(storage.data(), kCount,
                                                            perm::mint_permission_root<RegionWhole>());
    auto whole = spawn::mint_parallel_for<1>(ctx, dram_bound_budget(), std::move(region), [](auto& shard) noexcept {
        for (int& element : shard) {
            element = 9;
        }
    });

    for (std::size_t index = 0; index < kCount; ++index) {
        if (storage[index] != 9) {
            std::fprintf(stderr, "the single-shard body did not run: element %zu is %d\n", index, storage[index]);
            return 1;
        }
    }
    if (whole.size() != kCount) {
        std::fprintf(stderr, "the single-shard recombine lost elements: %zu of %zu\n", whole.size(), kCount);
        return 1;
    }
    return 0;
}

// The thread each shard ran on, for a region with one element per shard.
// Element i holds i, so a body learns its shard from its element.
template <std::size_t Shards>
struct ShardThreads {
    std::array<std::uint32_t, Shards> storage{};
    std::array<std::thread::id, Shards> thread_of_shard{};
    std::array<int, Shards> runs_of_shard{};

    // Runs the shards under one budget and records who ran each one.
    void run(fixy::concurrent::WorkBudget budget) noexcept {
        for (std::uint32_t index = 0; index < Shards; ++index) {
            storage[index] = index;
        }
        BgCtx ctx{eff::testing::bg()};
        auto region = fixy::mint_owned_region(storage.data(), Shards, perm::mint_permission_root<RegionWhole>());
        auto whole = spawn::mint_parallel_for<Shards>(ctx, budget, std::move(region), [this](auto& shard) noexcept {
            for (const std::uint32_t index : shard) {
                thread_of_shard[index] = std::this_thread::get_id();
                ++runs_of_shard[index];
            }
        });
        (void)whole;
    }

    [[nodiscard]] bool every_shard_ran_once() const noexcept {
        return std::all_of(runs_of_shard.begin(), runs_of_shard.end(), [](int runs) noexcept { return runs == 1; });
    }

    [[nodiscard]] std::size_t distinct_threads() const noexcept {
        std::set<std::thread::id> threads(thread_of_shard.begin(), thread_of_shard.end());
        return threads.size();
    }

    [[nodiscard]] bool ran_on(std::thread::id thread) const noexcept {
        return std::find(thread_of_shard.begin(), thread_of_shard.end(), thread) != thread_of_shard.end();
    }
};

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
        std::fprintf(stderr, "mint_parallel_for: a core-resident budget must run every shard on the calling thread, "
                             "but the shards ran on %zu threads\n",
                     record.distinct_threads());
        return 1;
    }
    return 0;
}

// Past L3 the rule goes parallel.  The mint starts no more threads than the
// factor of the decision, never uses the calling thread for a shard, and
// runs each shard on exactly one thread.
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
    if (want_threads > 1 && record.ran_on(std::this_thread::get_id())) {
        std::fprintf(stderr, "mint_parallel_for: a threaded run put a shard on the calling thread\n");
        return 1;
    }
    return 0;
}

// Inside the shared L3 the rule caps the factor below the shard count, so
// sixteen shards share a few threads.
[[nodiscard]] int parallel_for_l3_resident_shares_threads() {
    const auto& topo = fixy::concurrent::Topology::instance();
    const fixy::concurrent::WorkBudget budget{.read_bytes = (topo.l2_per_core_bytes() + topo.l3_total_bytes()) / 2};
    const auto decision = fixy::concurrent::ParallelismRule::recommend(budget);
    ShardThreads<16> record{};
    record.run(budget);
    if (!record.every_shard_ran_once()) {
        std::fprintf(stderr, "mint_parallel_for: a shard of the L3-resident run did not run exactly once\n");
        return 1;
    }
    const std::size_t want_threads = decision.is_parallel() ? std::min<std::size_t>(16, decision.factor) : 1;
    if (record.distinct_threads() > want_threads || want_threads >= 16) {
        std::fprintf(stderr, "mint_parallel_for: an L3-resident run used %zu threads for 16 shards, and the rule "
                             "allows %zu\n",
                     record.distinct_threads(), want_threads);
        return 1;
    }
    return 0;
}

}  // namespace

int main() {
    if (const int rc = spawn_runs_every_child_and_returns_the_parent(); rc != 0) return rc;
    if (const int rc = spawn_arm_follows_the_budget(); rc != 0) return rc;
    if (const int rc = parallel_for_visits_every_element_once(); rc != 0) return rc;
    if (const int rc = parallel_for_with_one_shard_runs_inline(); rc != 0) return rc;
    if (const int rc = parallel_for_core_resident_runs_inline(); rc != 0) return rc;
    if (const int rc = parallel_for_dram_bound_caps_threads_at_the_factor(); rc != 0) return rc;
    if (const int rc = parallel_for_l3_resident_shares_threads(); rc != 0) return rc;
    return 0;
}
