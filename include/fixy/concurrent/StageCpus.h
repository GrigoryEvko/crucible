#pragma once

// The CPUs of the stages of a threaded pipeline run.
//
// A threaded run pins the thread of each stage to one CPU of the mask of the
// caller.  A stage that spins then keeps a CPU of its own, and no other stage
// of a pipeline spins on that CPU.
//
// THE ORDER OF THE CANDIDATES
//     The candidates of a run are the CPUs of the mask, in the order of the
//     cache clusters of the host.  The cluster with the most CPUs of the mask
//     comes first, so the stages of a small run share one last-level cache.
//     The other clusters come next, by the same count, and a tie keeps the
//     order of the topology.  Each CPU of the mask that no cluster names comes
//     last, in increasing order.  A CPU id past AffinityMask::kMaxCore is no
//     candidate.
//
// THE CLAIMS
//     Each stage claims its CPU in a table of claimed CPUs, and the run gives
//     each claim back after its join.  A stage takes the first candidate that
//     no live stage holds.  So two stages never share a pinned CPU, in one run
//     or in two runs at the same time.  A stage with no free candidate gets no
//     CPU: it runs unpinned and keeps the mask of the caller, and the
//     scheduler places it.
//
//     A claim is an acquire-release fetch-or on the word of its CPU, and a
//     give-back is an acquire-release fetch-and.  A claim that takes a CPU
//     thus sees each write of the run that gave the CPU back.
//
// THE TABLE OF THE PROCESS
//     process_stage_cpu_claims() gives the one table of the process, a
//     ProcessStageCpuClaims with a brand of its own.  The
//     table is an object of src/fixy/concurrent/StageCpus.cpp with the marker
//     CRUCIBLE_PROCESS_WIDE, so each shared library that links libfixy binds
//     to one table.  A test builds a table of its own, and a planted topology
//     of Topology::probe_tree, to plan runs on a host that it chooses.

#include <foundation/Pinned.h>
#include <foundation/algebra/lattices/AffinityLattice.h>
#include <foundation/core/Array.h>
#include <foundation/core/Atomic.h>
#include <foundation/core/Index.h>
#include <foundation/core/List.h>
#include <foundation/core/Ref.h>
#include <foundation/core/Region.h>

#include <cstddef>
#include <cstdint>

namespace fixy::concurrent {

// The CPUs that the pinned stages of the live runs hold: one bit for each CPU
// id of an AffinityMask.  The run that claims a bit is the one writer that
// gives it back.
class StageCpuClaims : ::foundation::Pinned<StageCpuClaims> {
public:
    static constexpr std::size_t word_count = ::foundation::algebra::lattices::AffinityMask::kWords;

    constexpr StageCpuClaims() noexcept = default;

    // Claims the CPU for one stage.  True when no stage held it.
    [[nodiscard]] bool try_claim(std::uint16_t cpu) noexcept;

    // Gives back a CPU that try_claim gave.  A CPU that no stage holds ends
    // the process, because the run gave it back two times.
    void give_back(std::uint16_t cpu) noexcept;

    // True when a stage holds the CPU.  A snapshot for a test, because a
    // different run can claim or give back the CPU after the read.
    [[nodiscard]] bool is_claimed(std::uint16_t cpu) const noexcept;

private:
    ::foundation::core::FixedArray<::foundation::core::Atomic<std::uint64_t>, word_count> words_{};
};

// The type of the one table of the process.  Its brand names that one table,
// so each Ref of the table carries the brand.
class ProcessStageCpuClaims : public StageCpuClaims {
public:
    struct brand_type {};
};

// The one table of the process (THE TABLE OF THE PROCESS).
[[nodiscard]] ::foundation::core::Ref<ProcessStageCpuClaims, ProcessStageCpuClaims::brand_type>
process_stage_cpu_claims() noexcept;

namespace detail {

// The candidates of one run in their order, each CPU of the mask at most one
// time (THE ORDER OF THE CANDIDATES).
//
// The members that read the mask are in this header.  AffinityMask::contains
// has a contract precondition, and a contract in an object of libfixy makes
// that object call std::terminate, which the rule artifact-symbols refuses.
class StageCpuOrder {
    using AffinityMask = ::foundation::algebra::lattices::AffinityMask;
    using Position = ::foundation::core::Index<AffinityMask::kBits>;

public:
    explicit StageCpuOrder(AffinityMask const& allowed) noexcept : allowed_{allowed} {}

    // Puts the CPU at the end of the order, when the mask holds it and the
    // order does not.  A negative CPU id, or one past kMaxCore, is no
    // candidate.
    void add(int cpu) noexcept {
        if (!is_open(cpu)) return;
        for (Position const position : position_of_(cpu)) {
            is_ordered_.at(position) = true;
            static_cast<void>(order_.push(static_cast<std::uint16_t>(position.raw()))
                                  .expect("the order holds each CPU of the mask at most one time"));
        }
    }

    // Puts each CPU of the mask that the order does not hold at the end, in
    // increasing order.  Complexity: O(AffinityMask::kBits).
    void add_rest_of_mask() noexcept {
        for (std::size_t cpu = 0; cpu < AffinityMask::kBits; ++cpu) {
            add(static_cast<int>(cpu));
        }
    }

    // The count of the CPUs of a cluster that the order can still take.
    // Complexity: O(size of the cluster).
    template <class Cluster>
    [[nodiscard]] std::size_t count_open(Cluster const& cluster) const noexcept {
        std::size_t open = 0;
        for (int const cpu : cluster) {
            if (is_open(cpu)) ++open;
        }
        return open;
    }

    // Writes the first free candidates into cpus, one for each stage, and -1
    // for each stage past the free candidates.  Complexity: O(candidates).
    void claim_into(StageCpuClaims& claims, ::foundation::core::View<int> cpus) const noexcept;

private:
    // The position of a CPU id in a mask, or no value for a negative id or one
    // past kMaxCore.
    [[nodiscard]] static ::foundation::core::Option<Position> position_of_(int cpu) noexcept {
        if (cpu < 0) return ::foundation::core::none;
        return Position::check(static_cast<std::size_t>(cpu));
    }

    // True when the mask holds the CPU and the order does not.
    [[nodiscard]] bool is_open(int cpu) const noexcept {
        for (Position const position : position_of_(cpu)) {
            return allowed_.contains(static_cast<std::uint16_t>(position.raw())) && !is_ordered_.at(position);
        }
        return false;
    }

    AffinityMask allowed_{};
    ::foundation::core::FixedArray<bool, AffinityMask::kBits> is_ordered_{};
    ::foundation::core::FixedList<std::uint16_t, AffinityMask::kBits> order_{};
};

}  // namespace detail

// Writes the CPU of each stage of one run into cpus, in the order of the
// stages, or -1 for a stage with no free candidate (THE ORDER OF THE
// CANDIDATES, THE CLAIMS).  allowed is the mask of the caller, and an empty
// mask gives -1 to each stage.  clusters lists the cache clusters, each one a
// list of CPU ids, as Topology::cache_clusters() gives them.  Complexity:
// O(K * E + B) for K clusters, E CPU entries in all clusters and
// B = AffinityMask::kBits, one time for each run, next to the start of one
// thread for each stage.
template <class Clusters>
void claim_stage_cpus(StageCpuClaims& claims, ::foundation::algebra::lattices::AffinityMask const& allowed,
                      Clusters const& clusters, ::foundation::core::View<int> cpus) noexcept {
    detail::StageCpuOrder order{allowed};

    // Each pass puts the cluster with the most open CPUs into the order.  A
    // cluster in the order has no open CPU, so the passes end.  A tie keeps
    // the first cluster.
    for (;;) {
        std::size_t best_open = 0;
        std::size_t best_position = 0;
        std::size_t position = 0;
        for (auto const& cluster : clusters) {
            std::size_t const open = order.count_open(cluster);
            if (open > best_open) {
                best_open = open;
                best_position = position;
            }
            ++position;
        }
        if (best_open == 0) break;
        position = 0;
        for (auto const& cluster : clusters) {
            if (position == best_position) {
                for (int const cpu : cluster) {
                    order.add(cpu);
                }
            }
            ++position;
        }
    }
    order.add_rest_of_mask();
    order.claim_into(claims, cpus);
}

// Gives back each CPU of cpus that is not -1, and writes -1 over it.
// Complexity: O(cpus.size()).
void give_back_stage_cpus(StageCpuClaims& claims, ::foundation::core::View<int> cpus) noexcept;

}  // namespace fixy::concurrent
