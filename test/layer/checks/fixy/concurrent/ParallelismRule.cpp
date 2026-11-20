// The compile-time checks of fixy/concurrent/ParallelismRule.h.

#include <fixy/concurrent/ParallelismRule.h>

#include <foundation/core/Number.h>

namespace fixy::concurrent {

namespace detail::parallelism_rule_self_test {

namespace ladder = parallelism_rule_detail;

inline constexpr std::size_t kMaxSize = ::foundation::core::max_of<std::size_t>;

// The ladder rounds down and tops out at the ceiling.
static_assert(ladder::round_to_factor_ladder(0) == 1);
static_assert(ladder::round_to_factor_ladder(1) == 1);
static_assert(ladder::round_to_factor_ladder(3) == 2);
static_assert(ladder::round_to_factor_ladder(7) == 4);
static_assert(ladder::round_to_factor_ladder(15) == 8);
static_assert(ladder::round_to_factor_ladder(16) == ladder::kMaxFactor);
static_assert(ladder::round_to_factor_ladder(kMaxSize) == ladder::kMaxFactor);
static_assert(ladder::round_to_factor_ladder(ladder::kL3ResidentMaxFactor) == ladder::kL3ResidentMaxFactor);

// Design note 1: neither the sum nor the product wraps.
static_assert(WorkBudget{.read_bytes = kMaxSize, .write_bytes = 1}.working_set_bytes() == kMaxSize);
static_assert(WorkBudget{.read_bytes = 3, .write_bytes = 4}.working_set_bytes() == 7);
static_assert(ParallelismRule::budget_for_span<std::uint64_t>(kMaxSize / 2).read_bytes == kMaxSize);
static_assert(ParallelismRule::budget_for_span<std::uint64_t>(1024).write_bytes == 1024 * sizeof(std::uint64_t));
static_assert(ParallelismRule::budget_for_span<std::uint64_t>(1024).item_count == 1024);

// A default decision is the sequential one.
static_assert(!ParallelismDecision{}.is_parallel());
static_assert(ParallelismDecision{}.factor == 1);

// A factor of one is the sequential decision.  A process of one CPU, or a
// largest L3 group of one CPU, gives it for a set past L2.
inline constexpr HostFacts kOneCpu{.l1d_bytes = 32 << 10,
                                   .l2_bytes = 1 << 20,
                                   .l3_bytes = 32 << 20,
                                   .process_cpus = 1,
                                   .largest_l3_group_cpus = 8,
                                   .numa_nodes = 2};
inline constexpr HostFacts kGroupOfOne{.l1d_bytes = 32 << 10,
                                       .l2_bytes = 1 << 20,
                                       .l3_bytes = 32 << 20,
                                       .process_cpus = 16,
                                       .largest_l3_group_cpus = 1,
                                       .numa_nodes = 2};
inline constexpr WorkBudget kL3Set{.read_bytes = 8 << 20, .write_bytes = 0};
inline constexpr WorkBudget kDramSet{.read_bytes = std::size_t{256} << 20, .write_bytes = 0};
static_assert(!ParallelismRule::recommend_for(kOneCpu, kL3Set).is_parallel());
static_assert(ParallelismRule::recommend_for(kOneCpu, kL3Set).factor == 1);
static_assert(ParallelismRule::recommend_for(kOneCpu, kL3Set).tier == Tier::L3Resident);
static_assert(!ParallelismRule::recommend_for(kOneCpu, kDramSet).is_parallel());
static_assert(ParallelismRule::recommend_for(kOneCpu, kDramSet).numa == NumaPolicy::NumaIgnore);
static_assert(!ParallelismRule::recommend_for(kGroupOfOne, kL3Set).is_parallel());
static_assert(ParallelismRule::recommend_for(kGroupOfOne, kDramSet).factor == 16);
static_assert(ParallelismRule::recommend_for(kGroupOfOne, kDramSet).numa == NumaPolicy::NumaSpread);

// Design note 4: a set whose work does not pay for the starts of two threads
// stays sequential, and the start cost picks the factor with the least cost.
// The host is the bench VM: an L3 of 16 MiB for 8 CPUs, and 9 CPUs.
inline constexpr HostFacts kStartCostHost{.l1d_bytes = 32 << 10,
                                          .l2_bytes = 1 << 20,
                                          .l3_bytes = 16 << 20,
                                          .process_cpus = 9,
                                          .largest_l3_group_cpus = 8,
                                          .numa_nodes = 2,
                                          .thread_start_bytes = ladder::kThreadStartBytes};
inline constexpr WorkBudget kWalk{.read_bytes = 8 << 20, .write_bytes = 4 << 20};
static_assert(!ParallelismRule::recommend_for(kStartCostHost, kWalk).is_parallel());
static_assert(ParallelismRule::recommend_for(kStartCostHost, WorkBudget{.read_bytes = 16 << 20}).factor == 2);
static_assert(ParallelismRule::recommend_for(kStartCostHost, WorkBudget{.read_bytes = std::size_t{192} << 20}).factor
              == 8);

static_assert(is_core_private_tier(Tier::L1Resident));
static_assert(is_core_private_tier(Tier::L2Resident));
static_assert(!is_core_private_tier(Tier::L3Resident));
static_assert(!is_core_private_tier(Tier::DRAMBound));

}  // namespace detail::parallelism_rule_self_test

}  // namespace fixy::concurrent
