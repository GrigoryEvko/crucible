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

#include <crucible/Platform.h>
#include <crucible/concurrent/Topology.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace crucible::concurrent {

// A duplicate of the budget type in the workload layer, kept here so
// that layer can include this header without a cycle.

struct WorkBudget {
    std::size_t read_bytes = 0;
    std::size_t write_bytes = 0;
    std::size_t item_count = 0;  // informational; the rule ignores it
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

class ParallelismRule {
public:
    ParallelismRule() = delete;

    // The boundaries come from the host, so one byte count classifies
    // differently on two machines.
    [[nodiscard, gnu::pure]] static Tier classify(std::size_t ws_bytes) noexcept {
        const auto& topo = Topology::instance();
        const std::size_t l1d = topo.l1d_per_core_bytes();
        const std::size_t l2 = topo.l2_per_core_bytes();
        const std::size_t l3 = topo.l3_total_bytes();
        if (ws_bytes < l1d) return Tier::L1Resident;
        if (ws_bytes < l2) return Tier::L2Resident;
        if (ws_bytes < l3) return Tier::L3Resident;
        return Tier::DRAMBound;
    }

    // Deterministic: the same host and the same budget always give the
    // same answer.  There is no randomness and no per-call-site state.
    [[nodiscard]] static ParallelismDecision recommend(WorkBudget budget) noexcept {
        const auto& topo = Topology::instance();
        const std::size_t ws = budget.read_bytes + budget.write_bytes;

        // What the process may run on, which under a CPU quota is less
        // than what the host has.
        const std::size_t cores_avail = std::max(std::size_t{1}, topo.process_cpu_count());
        const std::size_t cores_per_socket = std::max(std::size_t{1}, topo.cores_per_socket());

        ParallelismDecision dec;
        dec.tier = classify(ws);

        // The set is already hot in one core's private cache, so a
        // second worker buys invalidation traffic and nothing else.
        if (dec.tier == Tier::L1Resident || dec.tier == Tier::L2Resident) {
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

    // Duplicated from the workload layer for the same reason the budget
    // type is, so a consumer of this rule alone pulls in no more.
    template <typename T>
    [[nodiscard]] static constexpr WorkBudget budget_for_span(std::size_t count) noexcept {
        const std::size_t bytes = count * sizeof(T);
        return WorkBudget{
            .read_bytes = bytes,
            .write_bytes = bytes,
            .item_count = count,
        };
    }
};

[[nodiscard]] inline ParallelismDecision recommend_parallelism(WorkBudget budget) noexcept {
    return ParallelismRule::recommend(budget);
}

}  // namespace crucible::concurrent
