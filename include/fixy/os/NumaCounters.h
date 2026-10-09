#pragma once

// The allocation counters of one NUMA node, from the file
// /sys/devices/system/node/nodeN/numastat.
//
// The kernel counts each page that it allocates.  numa_foreign counts each
// allocation that preferred the node and got a page of another node: the
// fallback that a first touch does not report.  A caller that reads the
// counters of a node before and after it touches a range knows that no
// allocation that preferred the node fell back in that time when
// numa_foreign stayed the same.  The counters belong to the whole host, so
// a change can also come from another process.
//
// The read goes through the file door of the base, foundation/os/File.h.
// The read can park the caller, so the gate is the gate of the census: the
// context of the caller owns IO and Block.  The path, the read and the parse
// are in src/fixy/os/NumaCounters.cpp.

#include <fixy/Core.h>
#include <fixy/os/NumaPlacement.h>
#include <foundation/effects/Ctx.h>

#include <cstdint>

namespace fixy::numa {

// The six counters of a node.  Each one counts pages since the boot.
struct NodeAllocationCounters {
    std::uint64_t hit = 0;  // numa_hit: the node gave a page to an allocation that preferred it
    std::uint64_t miss = 0;  // numa_miss: the node gave a page to an allocation that preferred another node
    std::uint64_t foreign = 0;  // numa_foreign: an allocation preferred the node and got a page of another node
    std::uint64_t interleave_hit = 0;  // interleave_hit: an interleaved allocation got a page of the node it chose
    std::uint64_t local = 0;  // local_node: a process on a CPU of the node got a page of the node
    std::uint64_t other = 0;  // other_node: a process on a CPU of another node got a page of the node
};

// The counters in the text of a numastat file: one line for each counter,
// a name and a decimal count.  A line with another name is skipped, so a
// counter that a later kernel adds changes nothing.  The error has the step
// CounterFormat when a counter is missing, when a counter has two lines, or
// when a line has no count or a count past the range of 64 bits.
// Complexity: O(characters).
[[nodiscard]] Result<NodeAllocationCounters, NumaQueryError> parse_allocation_counters(TextView text) noexcept;

namespace detail {

// The counters in the numastat file of a concrete node.
[[nodiscard]] Result<NodeAllocationCounters, NumaQueryError> read_allocation_counters_(NumaNodeId node) noexcept;

}  // namespace detail

// The counters of the node.  The node of a host that has no such node, and
// a kernel with no NUMA support, give the step CounterRead with the errno
// that the file door gives.
//
// Not a mint: the read synthesizes no authority.
template <::foundation::effects::IsExecCtx Ctx>
    requires CtxFitsNumaQuery<Ctx>
[[nodiscard]] Result<NodeAllocationCounters, NumaQueryError> read_allocation_counters(Ctx const&,
                                                                                      NumaNodeId node) noexcept {
    if (!is_concrete_numa_node(node)) {
        return err(NumaQueryError{NumaQueryStep::NodeNotConcrete, 0});
    }
    return detail::read_allocation_counters_(node);
}

}  // namespace fixy::numa
