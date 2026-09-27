#pragma once

// Decides whether a body runs inline or in parallel, and at what factor
// and NUMA policy.  The decision comes from the working set's size
// against the host's cache hierarchy and from nothing else.  There is
// no per-item time estimate and no caller hint, because a hint of that
// kind cannot be checked and is wrong exactly when it hurts most.
//
// The commitment is that the rule never regresses.  A working set that
// fits a core's L2 is already hot in that core, so a second worker adds
// invalidation traffic and no bandwidth, and the rule keeps it
// sequential.  Past L2 the factor is still capped by what the memory
// hierarchy can stream.  Inside the shared L3 the workers stay on one
// socket, so coherence traffic does not cross it.  Beyond L3 the work
// is bandwidth-bound, the factor tracks how many L2-sized pieces the
// set divides into, and spreading over NUMA nodes puts independent
// memory controllers to work.
//
// Its honest scope is the data side.  A caller that wants parallelism
// because the computation is expensive, over data that is cheap, is
// outside this rule and should dispatch directly.
//
// The rejected alternative is per-call-site autotuning.  It needs
// persistent state at every site, regresses until it converges, and
// gives up determinism.
//
// The factor snaps to a fixed ladder of powers of two, so the
// dispatcher's switch becomes a small jump table.  It rounds down, so
// the rule under-spawns rather than over-spawns.
//
// Every cap comes from the count of CPUs the process may actually run
// on, not from the host's core count.  In a container those differ, and
// spawning to the host's count would leave the threads time-slicing on
// a fraction of that many CPUs, which is worse than staying sequential.
//
// This header is the one place the cache thresholds live.  The rule
// reads the probed sizes at run time.  fixy/os/Spawn.h and
// fixy/concurrent/Pipeline.h ask is_core_resident or recommend rather
// than compare sizes of their own.
//
// Old spelling: include/crucible/concurrent/ParallelismRule.h.
//
// Deviations, each deliberate:
//
//  1. One WorkBudget.  The old header kept a duplicate of the budget type
//     of the workload layer, so that layer could include it without a
//     cycle.  The workload layer did not come to this tree, so this is
//     the only budget type.
//
//  2. The working set is the saturated sum of the read and written bytes,
//     and budget_for_span saturates its product.  The old sum and product
//     could wrap, and a wrapped size reads as a small set, so the rule
//     kept a huge set sequential.
//
//  3. One boundary rule: a set fits a tier when it is no larger than the
//     capacity of the tier.  The old classify compared strictly, while the
//     old dispatch choice of Pipeline compared with no-larger-than.  A set
//     of exactly L2 bytes was then L3-resident to the rule and
//     core-resident to Pipeline.
//
//  4. is_core_private_tier and is_core_resident name the one question the
//     callers ask.  The old callers each wrote their own comparison.
//
//  5. The static tier math did not come: the fleet floors of
//     TopologyConstexpr.h, fits_in_tier_v, required_tier_for_footprint and
//     the substrate gates of SubstrateCtxFit.h.  The gates read the
//     substrate descriptor and the residency axis of the old context, and
//     neither exists in this tree.  Without the gates, nothing read the
//     math.  The recommend_parallelism alias did not come either, because
//     each caller names ParallelismRule::recommend.  The conservative
//     floors of WorkingSet.h stay, because a compile-time check that must
//     hold on the smallest supported host reads them.
//
//  6. ParallelismRule deletes every constructor and has a user-provided
//     destructor, the shape of the other static-only holders in this tree.
//     The old class deleted only its default constructor, so it stayed
//     trivially copyable, and a byte route could make an object of it.

#include <fixy/concurrent/Topology.h>
#include <foundation/Saturate.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace fixy::concurrent {

struct WorkBudget {
    std::size_t read_bytes = 0;
    std::size_t write_bytes = 0;
    std::size_t item_count = 0;  // telemetry only: the rule does not read it

    // The bytes the work touches.  The sum saturates, because a wrapped
    // sum reads as a small set and would keep a huge one sequential.
    [[nodiscard]] constexpr std::size_t working_set_bytes() const noexcept {
        return ::foundation::sat::add_sat(read_bytes, write_bytes);
    }
};

enum class Tier : std::uint8_t {
    L1Resident = 0,
    L2Resident = 1,
    L3Resident = 2,
    DRAMBound = 3,
};

// An intent, not a binding.  The worker placement happens in the
// scheduler and the thread pool.
//
//   NumaIgnore  leaves placement to the OS scheduler.
//   NumaLocal   holds every worker on one node.
//   NumaSpread  distributes workers across nodes in proportion to
//               their core counts.

enum class NumaPolicy : std::uint8_t {
    NumaIgnore = 0,
    NumaLocal = 1,
    NumaSpread = 2,
};

// The tier and the policy carry the rationale for the decision, which
// is what telemetry and worker placement want.

struct ParallelismDecision {
    enum class Kind : std::uint8_t {
        Sequential = 0,
        Parallel = 1,
    };

    Kind kind = Kind::Sequential;
    std::size_t factor = 1;
    NumaPolicy numa = NumaPolicy::NumaIgnore;
    Tier tier = Tier::L1Resident;

    [[nodiscard]] constexpr bool is_parallel() const noexcept { return kind == Kind::Parallel; }
};

// A set in L1 or in L2 is in the private cache of one core.
[[nodiscard]] constexpr bool is_core_private_tier(Tier tier) noexcept {
    return tier == Tier::L1Resident || tier == Tier::L2Resident;
}

namespace parallelism_rule_detail {

// The ceiling on any factor, even on a very large machine.  Past this
// the lock-step join cost tends to eat the speedup.
inline constexpr std::size_t kMaxFactor = 16;

// Workers past this number thrash the shared L3 rather than add
// bandwidth to a set that already fits in it.
inline constexpr std::size_t kL3ResidentMaxFactor = 4;

// Rounds down, so a request between two ladder entries under-spawns.
[[nodiscard]] constexpr std::size_t round_to_factor_ladder(std::size_t want) noexcept {
    if (want >= 16) return 16;
    if (want >= 8) return 8;
    if (want >= 4) return 4;
    if (want >= 2) return 2;
    return 1;
}

}  // namespace parallelism_rule_detail

// Stateless.  The cache sizes are read at decision time, by which point
// the topology has already probed them.
//
// No object of the rule exists.  Every constructor is deleted and the
// destructor is user-provided, so the class is neither trivially copyable
// nor an implicit-lifetime type, and no byte route (std::bit_cast,
// std::start_lifetime_as) can make one either.

class ParallelismRule final {
public:
    ParallelismRule() = delete("the parallelism rule holds static members only; no object of it exists");
    ParallelismRule(const ParallelismRule&) = delete("the parallelism rule holds static members only");
    ParallelismRule& operator=(const ParallelismRule&) = delete("the parallelism rule holds static members only");
    ParallelismRule(ParallelismRule&&) = delete("the parallelism rule holds static members only");
    ParallelismRule& operator=(ParallelismRule&&) = delete("the parallelism rule holds static members only");
    constexpr ~ParallelismRule() noexcept {}

    // The boundaries come from the host, so one byte count classifies
    // differently on two machines.
    [[nodiscard, gnu::pure]] static Tier classify(std::size_t ws_bytes) noexcept {
        const auto& topo = Topology::instance();
        const std::size_t l1d = topo.l1d_per_core_bytes();
        const std::size_t l2 = topo.l2_per_core_bytes();
        const std::size_t l3 = topo.l3_total_bytes();
        if (ws_bytes <= l1d) return Tier::L1Resident;
        if (ws_bytes <= l2) return Tier::L2Resident;
        if (ws_bytes <= l3) return Tier::L3Resident;
        return Tier::DRAMBound;
    }

    // The question a caller asks when it chooses between inline and
    // threaded work.  A set in one core's private cache is already hot
    // there, so a second worker buys invalidation traffic and nothing else.
    [[nodiscard, gnu::pure]] static bool is_core_resident(std::size_t ws_bytes) noexcept {
        return is_core_private_tier(classify(ws_bytes));
    }

    // Deterministic: the same host and the same budget always give the
    // same answer.  There is no randomness and no per-call-site state.
    [[nodiscard]] static ParallelismDecision recommend(WorkBudget budget) noexcept {
        const auto& topo = Topology::instance();
        const std::size_t ws = budget.working_set_bytes();

        // What the process may run on, which under a CPU quota is less
        // than what the host has.
        const std::size_t cores_avail = std::max(std::size_t{1}, topo.process_cpu_count());
        const std::size_t cores_per_socket = std::max(std::size_t{1}, topo.cores_per_socket());

        ParallelismDecision dec;
        dec.tier = classify(ws);

        // The set is already hot in one core's private cache, so a
        // second worker buys invalidation traffic and nothing else.
        if (is_core_private_tier(dec.tier)) {
            dec.kind = ParallelismDecision::Kind::Sequential;
            dec.factor = 1;
            dec.numa = NumaPolicy::NumaIgnore;
            return dec;
        }

        // The set fits one socket's shared L3, so the workers stay on
        // that socket and the coherence traffic never leaves it.
        if (dec.tier == Tier::L3Resident) {
            const std::size_t want = std::min({
                cores_per_socket,
                cores_avail,
                parallelism_rule_detail::kL3ResidentMaxFactor,
            });
            dec.kind = ParallelismDecision::Kind::Parallel;
            dec.factor = parallelism_rule_detail::round_to_factor_ladder(want);
            dec.numa = NumaPolicy::NumaLocal;
            return dec;
        }

        // Bandwidth-bound.  The factor follows how many L2-sized pieces
        // the set divides into, and spreading over nodes recruits their
        // memory controllers.
        const std::size_t l2 = std::max(std::size_t{1}, topo.l2_per_core_bytes());
        const std::size_t want = std::min(cores_avail, std::max(std::size_t{1}, ws / l2));
        dec.kind = ParallelismDecision::Kind::Parallel;
        dec.factor = parallelism_rule_detail::round_to_factor_ladder(want);
        dec.numa = (topo.numa_nodes() > 1) ? NumaPolicy::NumaSpread : NumaPolicy::NumaIgnore;
        return dec;
    }

    // A span read and written in place.  The byte count saturates, for
    // the reason working_set_bytes gives.
    template <typename T>
    [[nodiscard]] static constexpr WorkBudget budget_for_span(std::size_t count) noexcept {
        const std::size_t bytes = ::foundation::sat::mul_sat(count, sizeof(T));
        return WorkBudget{
            .read_bytes = bytes,
            .write_bytes = bytes,
            .item_count = count,
        };
    }
};

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

// Deviation 2: neither the sum nor the product wraps.
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
