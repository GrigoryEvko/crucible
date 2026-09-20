// The two spawn mints, run for real: children that join, and shards that
// are recombined into the region they came from.
//
// Old spelling: test/test_fixy_v_203_spawn_join_policy.cpp and
// test/test_fixy_v_204_spawn_grant.cpp, both of which were entirely
// static_asserts. Those properties live in fixy/os/Spawn.h now, where
// they fire wherever the types are used. What is here is the behaviour:
// that every child body ran, that the parent Permission comes back, and
// that a recombined region still describes the same storage.

#include <fixy/os/Spawn.h>
#include <foundation/permissions/Permission.h>

#include <array>
#include <atomic>
#include <cstdio>
#include <type_traits>
#include <utility>

namespace eff = foundation::effects;
namespace perm = foundation::permissions;
namespace spawn = fixy::spawn;

namespace {

// The fork's spawning arm starts one thread per child, which is
// background work, so the context has to own Bg.
using BgCtx = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::Alloc>>;

struct Whole {};
struct Left {};
struct Right {};

struct RegionWhole {};

}  // namespace

// The partition has to be declared where the fork can see it: splitting a
// parent into children declares nothing by itself.
namespace foundation::permissions {
template <>
struct splits_into_pack<Whole, Left, Right> : std::true_type {};
template <>
struct splits_into_pack_authoring_witness<Whole, Left, Right> : std::true_type {};
}  // namespace foundation::permissions

namespace {

[[nodiscard]] int spawn_runs_every_child_and_returns_the_parent() {
    BgCtx ctx{eff::testing::bg()};
    auto whole = perm::mint_permission_root<Whole>();

    std::atomic<int> ran{0};

    auto rebuilt = spawn::mint_spawn<Left, Right>(
        ctx, std::move(whole),
        [&ran](perm::Permission<Left>, BgCtx const&) noexcept { ran.fetch_add(1, std::memory_order_acq_rel); },
        [&ran](perm::Permission<Right>, BgCtx const&) noexcept { ran.fetch_add(2, std::memory_order_acq_rel); });

    // 1 from the left body and 2 from the right: a total of 3 says both
    // ran, and says which one is missing if they did not.
    if (const int total = ran.load(std::memory_order_acquire); total != 3) {
        std::fprintf(stderr, "mint_spawn: child bodies contributed %d, want 3 (1 = left only, 2 = right only)\n",
                     total);
        return 1;
    }

    // The parent permission is back, which is what lets the caller keep
    // using the region the children borrowed.
    static_assert(std::is_same_v<decltype(rebuilt), perm::Permission<Whole>>,
                  "mint_spawn must hand the parent Permission back.");
    (void)rebuilt;
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
    auto whole = spawn::mint_parallel_for<kShards>(ctx, std::move(region), [](auto& shard) noexcept {
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
    auto whole = spawn::mint_parallel_for<1>(ctx, std::move(region), [](auto& shard) noexcept {
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

}  // namespace

int main() {
    if (const int rc = spawn_runs_every_child_and_returns_the_parent(); rc != 0) return rc;
    if (const int rc = parallel_for_visits_every_element_once(); rc != 0) return rc;
    if (const int rc = parallel_for_with_one_shard_runs_inline(); rc != 0) return rc;
    return 0;
}
