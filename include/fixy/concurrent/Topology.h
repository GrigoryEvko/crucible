#pragma once

// What the machine is: its caches, its cores, its NUMA nodes and its
// page size, read once from sysfs and held for the life of the process.
// Every figure a caller cannot read is a fallback that stands in for a
// measurement, deliberately above the conservative floors in
// WorkingSet.h, so an unmeasured host never reads as the smallest one.
//
// A NUMA node is named by its node id, and node ids can be sparse: a
// host can have node0 and node2 and no node1.  Every NUMA accessor takes
// a node id and never uses it as a position.
//
// The probe runs one time for each process.  Its body, with the sysfs
// reads and the three parsers below, is in src/fixy/concurrent/Topology.cpp
// in the library fixy, so an includer compiles none of it.

#include <foundation/Pinned.h>
#include <foundation/Platform.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fixy::concurrent {

namespace topology_detail {

// Sysfs writes cache sizes with a K, M or G suffix, or none at all.  Zero on
// a parse failure.
[[nodiscard]] std::size_t parse_size_suffix_(std::string_view s) noexcept;

// Sysfs writes CPU sets as comma-separated singletons and inclusive ranges,
// as in "0-3,8-11,16".  Empty on a parse failure.
[[nodiscard]] std::vector<int> parse_cpu_list_(std::string_view s) noexcept;

// One row of the NUMA distance matrix: integers separated by spaces.
[[nodiscard]] std::vector<int> parse_int_list_(std::string_view s) noexcept;

}  // namespace topology_detail

class Topology : public ::foundation::Pinned<Topology> {
public:
    enum class Source : std::uint8_t {
        Sysfs,
        Fallback,
    };

    struct Snapshot {
        std::size_t l1d_per_core_bytes = 0;
        std::size_t l1i_per_core_bytes = 0;
        std::size_t l2_per_core_bytes = 0;
        std::size_t l3_total_bytes = 0;
        std::size_t cache_line_bytes = 0;
        std::size_t num_cores = 0;
        std::size_t num_smt_threads = 0;
        std::size_t process_cpu_count = 0;
        std::size_t page_size_bytes = 0;
        std::size_t numa_nodes = 0;
        bool hugepage_2mb_available = false;
        Source source = Source::Fallback;
    };

    [[nodiscard]] static const Topology& instance() noexcept {
        static const Topology inst{kSysfsRoot};
        return inst;
    }

    [[nodiscard]] static Snapshot reprobe_snapshot() noexcept {
        const Topology fresh{kSysfsRoot};
        return fresh.snapshot();
    }

    // A topology read from a sysfs tree under another root, for a test
    // that builds the tree it wants, for example one with sparse node
    // ids.  Only a context that owns the Test capability reaches it.
    template <typename Ctx>
        requires ::foundation::effects::CtxOwnsCapability<Ctx, ::foundation::effects::Effect::Test>
    [[nodiscard]] static Topology probe_tree(Ctx const&, std::string_view sysfs_root) noexcept {
        return Topology{sysfs_root};
    }

    [[nodiscard]] Snapshot snapshot() const noexcept {
        return Snapshot{
            .l1d_per_core_bytes = l1d_,
            .l1i_per_core_bytes = l1i_,
            .l2_per_core_bytes = l2_,
            .l3_total_bytes = l3_,
            .cache_line_bytes = line_,
            .num_cores = cores_,
            .num_smt_threads = threads_,
            .process_cpu_count = process_cpus_,
            .page_size_bytes = page_size_,
            .numa_nodes = numa_nodes(),
            .hugepage_2mb_available = hugepage_2mb_,
            .source = source_,
        };
    }

    [[nodiscard]] std::size_t l1d_per_core_bytes() const noexcept { return l1d_; }
    [[nodiscard]] std::size_t l1i_per_core_bytes() const noexcept { return l1i_; }
    [[nodiscard]] std::size_t l2_per_core_bytes() const noexcept { return l2_; }
    [[nodiscard]] std::size_t l3_total_bytes() const noexcept { return l3_; }
    [[nodiscard]] std::size_t cache_line_bytes() const noexcept { return line_; }

    // Physical cores across every package, deduplicated on (package, core).
    [[nodiscard]] std::size_t num_cores() const noexcept { return cores_; }

    // Hardware threads the kernel reports ONLINE.  An offlined CPU keeps its
    // sysfs directory but does not count here, so this tracks hotplug and SMT
    // changes.  It is not the same question as `process_cpu_count()`, which
    // asks what this process is allowed to run on.
    [[nodiscard]] std::size_t num_smt_threads() const noexcept { return threads_; }

    // Threads per core, averaged over the machine and truncated.  On a part
    // with SMT disabled per core it is a FLOOR, not a per-core fact: the
    // bench host with 8 of its 192 cores SMT-disabled reports 1, though 184
    // still carry two threads.  Read `l3_groups()` when the caller needs to
    // know whether one specific core has a sibling.
    [[nodiscard]] std::size_t smt_factor() const noexcept { return cores_ == 0 ? 1 : threads_ / cores_; }

    // How many CPUs this process is allowed to run on, which under a container
    // or a cgroup is fewer than the machine has.  Thread-pool sizing and every
    // parallelism decision reads this rather than the core count, or it spawns
    // threads the process is not permitted to spread onto.
    [[nodiscard]] std::size_t process_cpu_count() const noexcept { return process_cpus_; }

    // Probed rather than assumed: it is 4 KB on most hosts but 16 KB on some
    // ARM ones.
    [[nodiscard]] std::size_t page_size_bytes() const noexcept { return page_size_; }

    // True when transparent hugepages are enabled, in either the always or the
    // ask-for-it mode.
    [[nodiscard]] bool hugepage_2mb_available() const noexcept { return hugepage_2mb_; }

    [[nodiscard]] std::string_view cpu_vendor() const noexcept { return cpu_vendor_; }
    [[nodiscard]] std::string_view cpu_model_name() const noexcept { return cpu_model_; }

    // One entry per L3 instance.  The inner spans list hardware thread ids,
    // not physical cores, so a machine with symmetric multithreading reports
    // each core more than once.

    [[nodiscard]] std::span<const std::vector<int>> l3_groups() const noexcept {
        return {l3_groups_.data(), l3_groups_.size()};
    }

    // An approximation: the largest group of threads sharing one L3.
    [[nodiscard]] std::size_t cores_per_socket() const noexcept {
        std::size_t m = 0;
        for (auto const& g : l3_groups_)
            m = std::max(m, g.size());
        return m == 0 ? cores_ : m;
    }

    // The same data under the name a chiplet-based part invites: there one L3
    // bounds a die, where on a monolithic part it bounds a socket.
    [[nodiscard]] std::span<const std::vector<int>> cache_clusters() const noexcept { return l3_groups(); }

    [[nodiscard]] std::size_t numa_nodes() const noexcept { return numa_node_ids_.size(); }

    // The node ids in ascending order.  They can be sparse.
    [[nodiscard]] std::span<const int> numa_node_ids() const noexcept {
        return {numa_node_ids_.data(), numa_node_ids_.size()};
    }

    // The distance between two nodes, each named by its node id.  Empty
    // when an id names no node of this host, so an unknown node never
    // reads as local.
    [[nodiscard]] std::optional<int> numa_distance(int from_node, int to_node) const noexcept {
        const std::optional<std::size_t> from = numa_position_(from_node);
        const std::optional<std::size_t> to = numa_position_(to_node);
        if (!from.has_value() || !to.has_value()) return std::nullopt;
        return numa_distance_[*from][*to];
    }

    // The CPUs of one node, named by its node id.  Empty when the id names
    // no node of this host.
    [[nodiscard]] std::span<const int> cores_on_node(int node) const noexcept {
        const std::optional<std::size_t> position = numa_position_(node);
        if (!position.has_value()) return {};
        return {cores_on_node_[*position].data(), cores_on_node_[*position].size()};
    }

    [[nodiscard]] Source source() const noexcept { return source_; }

    // Concurrent callers can interleave, but each line arrives whole.
    void log_summary(FILE* out = stderr) const noexcept;

private:
    // These stand in for a measurement the probe could not take.  They
    // are not the conservative floors in WorkingSet.h and must not be
    // replaced by them: a floor is a lower bound, so using one here
    // would make every unmeasured machine look like the smallest
    // supported one, and a workload would read as larger than the cache
    // whenever it is not.  That biases the cost model toward splitting
    // work that did not need splitting, which is the regression the
    // no-regression rule forbids.  Each figure is therefore deliberately
    // above the matching floor, and the ordering below is what keeps it
    // that way.
    static constexpr std::size_t kFallbackL1d = 32 * 1024;
    static constexpr std::size_t kFallbackL2 = 1024 * 1024;
    static constexpr std::size_t kFallbackL3 = 32ULL * 1024 * 1024;
    static_assert(kFallbackL1d < kFallbackL2 && kFallbackL2 < kFallbackL3);
    static constexpr std::size_t kFallbackLine = 64;

    static constexpr std::string_view kSysfsRoot = "/sys";

    std::size_t l1d_ = kFallbackL1d;
    std::size_t l1i_ = kFallbackL1d;  // L1i typically same as L1d
    std::size_t l2_ = kFallbackL2;
    std::size_t l3_ = kFallbackL3;
    std::size_t line_ = kFallbackLine;
    std::size_t cores_ = 1;
    std::size_t threads_ = 1;
    std::size_t process_cpus_ = 1;
    std::size_t page_size_ = 4096;
    bool hugepage_2mb_ = false;
    std::string cpu_vendor_;
    std::string cpu_model_;
    std::vector<std::vector<int>> l3_groups_;

    // One entry per NUMA node, in ascending order of node id.  The three
    // vectors are parallel: position i holds the id, the CPUs and the
    // distance row of one node.  A sysfs distance row lists the distance
    // to each online node in the same order, so a row also indexes by
    // position, and each row has one entry per node.  A lookup by id goes
    // through numa_position_ and never uses the id as a position.
    std::vector<int> numa_node_ids_;
    std::vector<std::vector<int>> cores_on_node_;
    std::vector<std::vector<int>> numa_distance_;
    Source source_ = Source::Fallback;

    // The position of a node id.  Complexity: logarithmic in the node
    // count, because the ids are sorted.
    [[nodiscard]] std::optional<std::size_t> numa_position_(int node) const noexcept {
        const auto found = std::lower_bound(numa_node_ids_.begin(), numa_node_ids_.end(), node);
        if (found == numa_node_ids_.end() || *found != node) return std::nullopt;
        return static_cast<std::size_t>(found - numa_node_ids_.begin());
    }

    // Noexcept and total: any failure to read sysfs, however malformed, leaves
    // the defaults in place rather than propagating out.
    explicit Topology(std::string_view sysfs_root) noexcept;

#if __has_include(<filesystem>)
    void probe_linux_(std::string_view sysfs_root) noexcept;
#endif
};

}  // namespace fixy::concurrent
