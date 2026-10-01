// The compile-time checks of fixy/concurrent/ParallelismRule.h.

#include <fixy/concurrent/ParallelismRule.h>

namespace fixy::concurrent {

namespace detail::parallelism_rule_self_test {

namespace ladder = parallelism_rule_detail;

inline constexpr std::size_t kMaxSize = std::numeric_limits<std::size_t>::max();

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

static_assert(is_core_private_tier(Tier::L1Resident));
static_assert(is_core_private_tier(Tier::L2Resident));
static_assert(!is_core_private_tier(Tier::L3Resident));
static_assert(!is_core_private_tier(Tier::DRAMBound));

}  // namespace detail::parallelism_rule_self_test

}  // namespace fixy::concurrent
