#pragma once

// ═══════════════════════════════════════════════════════════════════
// Topology — hardware introspection for the cost model
//
// One-shot startup probe of the host's compute hierarchy.  Parses
// Linux sysfs (or falls back to compile-time constants on non-Linux /
// /sys-less containers) into a cached singleton.  Provides cache
// sizes, core counts, L3 grouping, and NUMA distance matrix to the
// AdaptiveScheduler (SEPLOG-C3) and to `should_parallelize` in
// safety/Workload.h (currently using hardcoded constants).
//
// ─── The cost-model decision rule the Topology supports ────────────
//
//   ws = workload's total read+write bytes
//
//   if ws < l1d_per_core_bytes     → SEQUENTIAL  (already L1-resident)
//   if ws < l2_per_core_bytes      → SEQUENTIAL  (already L2-private)
//   if ws < l3_total_bytes / 8     → PARALLEL ≤ cores_per_socket
//   if ws ≥ l3_total_bytes (DRAM)  → PARALLEL = min(num_cores,
//                                                    ws / l2_per_core_bytes)
//                                    with NUMA-local affinity
//
// The cache thresholds come from Topology; the cost model (SEPLOG-C2)
// applies them.  Without Topology, the cost model has only generic
// fallback constants — Topology gives it ground truth.
//
// ─── What Topology provides (and explicitly does NOT) ──────────────
//
// Provides:
//   * Cache hierarchy: L1d, L2, L3, cache-line size
//   * Core counts: physical cores + SMT threads
//   * L3 groups: which cores share which L3 instance (= socket boundary)
//   * NUMA: node count, distance matrix, CPUs per node
//
// Does NOT (yet) provide:
//   * NUMA-local allocation (`numa_alloc_onnode` — needs libnuma; stub
//     returns the cores-on-node CPU set, leaving allocation to caller)
//   * Per-thread current-node detection (`getcpu` syscall — defer)
//   * Hugepage detection
//   * GPU/accelerator topology (Mimic territory; orthogonal)
//
// ─── The fallback discipline ────────────────────────────────────────
//
// On non-Linux, container without /sys, or sysfs read failure: the
// constructor logs once to stderr (in debug only) and populates
// reasonable defaults:
//
//   l1d  = 32 KB
//   l2   = 1 MB
//   l3   = 32 MB
//   line = 64 bytes
//   cores   = std::thread::hardware_concurrency() (always works)
//   threads = same
//   numa_nodes = 1
//   numa_distance(0, 0) = 10
//
// `source()` returns Source::Fallback so callers can tell.  The
// fallback values are conservative for cost-model decisions: they
// underestimate cache size, biasing toward parallelism (which is the
// safer error to make — over-parallelising at small workloads
// regresses by ~2× whereas under-parallelising at large workloads
// loses ~Nx).
//
// ─── Singleton lifetime ─────────────────────────────────────────────
//
// `Topology::instance()` returns a const reference to a function-
// local static.  C++11 guarantees thread-safe initialisation.  The
// probe runs exactly once, on the first instance() call from any
// thread; subsequent calls are zero-cost (atomic load + return).
//
// The class is Pinned (the singleton's address is the identity of
// the topology data; copying or moving would create stale references
// to invalidated cache).
// ═══════════════════════════════════════════════════════════════════

#include <crucible/Platform.h>
#include <crucible/safety/Pinned.h>

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <set>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#if __has_include(<filesystem>)
  #include <filesystem>
#endif

namespace crucible::concurrent {

namespace topology_detail {

// Read entire file content as a trimmed string.  Returns empty on
// failure (file missing, permission denied, etc.).  Trimming removes
// trailing whitespace (sysfs files end with newline).
[[nodiscard]] inline std::string read_trimmed_(std::string_view path) noexcept {
    try {
        std::ifstream f{std::string{path}};
        if (!f.is_open()) return {};
        std::string content;
        std::string line;
        while (std::getline(f, line)) {
            if (!content.empty()) content += ' ';
            content += line;
        }
        // Trim trailing whitespace.
        while (!content.empty() &&
               (content.back() == '\n' || content.back() == ' ' ||
                content.back() == '\t' || content.back() == '\r')) {
            content.pop_back();
        }
        return content;
    } catch (...) {
        return {};
    }
}

// Parse "32K" / "1024K" / "32M" / "1G" / "12345" → bytes.  Returns 0
// on parse failure.  Sysfs cache size files use 'K'/'M'/'G' suffixes.
[[nodiscard]] inline std::size_t parse_size_suffix_(std::string_view s) noexcept {
    if (s.empty()) return 0;
    // Find the suffix.
    char suffix = '\0';
    std::string_view digits = s;
    if (s.back() == 'K' || s.back() == 'M' || s.back() == 'G' ||
        s.back() == 'k' || s.back() == 'm' || s.back() == 'g') {
        suffix = static_cast<char>(s.back() | 0x20);  // tolower
        digits = s.substr(0, s.size() - 1);
    }
    std::size_t value = 0;
    auto [ptr, ec] = std::from_chars(digits.data(),
                                       digits.data() + digits.size(), value);
    if (ec != std::errc{}) return 0;
    switch (suffix) {
        case 'k': return value * 1024;
        case 'm': return value * 1024 * 1024;
        case 'g': return value * 1024 * 1024 * 1024;
        default:  return value;
    }
}

// Parse "0-3,8-11,16" → {0,1,2,3,8,9,10,11,16}.  Returns empty
// vector on parse failure.  Used for sysfs `shared_cpu_list` and
// `cpulist` files.
[[nodiscard]] inline std::vector<int>
parse_cpu_list_(std::string_view s) noexcept {
    std::vector<int> result;
    try {
        std::size_t pos = 0;
        while (pos < s.size()) {
            // Skip leading whitespace/separators.
            while (pos < s.size() && (s[pos] == ' ' || s[pos] == ',')) ++pos;
            if (pos >= s.size()) break;

            // Parse first number.
            int start = 0;
            auto [p1, ec1] = std::from_chars(s.data() + pos,
                                              s.data() + s.size(), start);
            if (ec1 != std::errc{}) return {};
            pos = static_cast<std::size_t>(p1 - s.data());

            int end = start;
            if (pos < s.size() && s[pos] == '-') {
                ++pos;
                auto [p2, ec2] = std::from_chars(s.data() + pos,
                                                  s.data() + s.size(), end);
                if (ec2 != std::errc{}) return {};
                pos = static_cast<std::size_t>(p2 - s.data());
            }
            for (int i = start; i <= end; ++i) result.push_back(i);
        }
    } catch (...) {
        return {};
    }
    return result;
}

// Parse "10 20 20 30" → {10, 20, 20, 30}.  Used for NUMA distance row.
[[nodiscard]] inline std::vector<int>
parse_int_list_(std::string_view s) noexcept {
    std::vector<int> result;
    try {
        std::size_t pos = 0;
        while (pos < s.size()) {
            while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t')) ++pos;
            if (pos >= s.size()) break;
            int v = 0;
            auto [p, ec] = std::from_chars(s.data() + pos,
                                            s.data() + s.size(), v);
            if (ec != std::errc{}) return {};
            pos = static_cast<std::size_t>(p - s.data());
            result.push_back(v);
        }
    } catch (...) {
        return {};
    }
    return result;
}

// Enumerate /sys/devices/system/cpu/cpu* directories that look like
// "cpuNNN" (digits only; not "cpufreq" / "cpuidle" / etc.).  Returns
// sorted vector of cpu IDs.
[[nodiscard]] inline std::vector<int> enumerate_cpus_() noexcept {
    std::vector<int> cpus;
#if __has_include(<filesystem>)
    namespace fs = std::filesystem;
    try {
        if (!fs::exists("/sys/devices/system/cpu")) return cpus;
        for (auto const& entry :
             fs::directory_iterator{"/sys/devices/system/cpu"}) {
            const auto name = entry.path().filename().string();
            if (name.size() < 4 || name.substr(0, 3) != "cpu") continue;
            const std::string_view tail{name.data() + 3, name.size() - 3};
            int id = 0;
            auto [p, ec] = std::from_chars(tail.data(),
                                            tail.data() + tail.size(), id);
            // Must consume entire tail (no trailing chars).
            if (ec != std::errc{} || p != tail.data() + tail.size()) continue;
            cpus.push_back(id);
        }
        std::sort(cpus.begin(), cpus.end());
    } catch (...) {
        cpus.clear();
    }
#endif
    return cpus;
}

// Enumerate /sys/devices/system/node/node* directories.
[[nodiscard]] inline std::vector<int> enumerate_numa_nodes_() noexcept {
    std::vector<int> nodes;
#if __has_include(<filesystem>)
    namespace fs = std::filesystem;
    try {
        if (!fs::exists("/sys/devices/system/node")) return nodes;
        for (auto const& entry :
             fs::directory_iterator{"/sys/devices/system/node"}) {
            const auto name = entry.path().filename().string();
            if (name.size() < 5 || name.substr(0, 4) != "node") continue;
            const std::string_view tail{name.data() + 4, name.size() - 4};
            int id = 0;
            auto [p, ec] = std::from_chars(tail.data(),
                                            tail.data() + tail.size(), id);
            if (ec != std::errc{} || p != tail.data() + tail.size()) continue;
            nodes.push_back(id);
        }
        std::sort(nodes.begin(), nodes.end());
    } catch (...) {
        nodes.clear();
    }
#endif
    return nodes;
}

}  // namespace topology_detail

// ── Topology singleton ──────────────────────────────────────────────

class Topology : public safety::Pinned<Topology> {
public:
    enum class Source : std::uint8_t {
        Sysfs,     // probed Linux /sys successfully
        Fallback,  // sysfs unavailable; using compile-time defaults
    };

    // Singleton accessor.  C++11-thread-safe function-local static.
    [[nodiscard]] static const Topology& instance() noexcept {
        static const Topology inst{};
        return inst;
    }

    // ── Cache hierarchy ─────────────────────────────────────────────

    [[nodiscard]] std::size_t l1d_per_core_bytes() const noexcept { return l1d_; }
    [[nodiscard]] std::size_t l2_per_core_bytes()  const noexcept { return l2_; }
    [[nodiscard]] std::size_t l3_total_bytes()     const noexcept { return l3_; }
    [[nodiscard]] std::size_t cache_line_bytes()   const noexcept { return line_; }

    // ── Core counts ─────────────────────────────────────────────────

    [[nodiscard]] std::size_t num_cores()       const noexcept { return cores_; }
    [[nodiscard]] std::size_t num_smt_threads() const noexcept { return threads_; }
    [[nodiscard]] std::size_t smt_factor()      const noexcept {
        return cores_ == 0 ? 1 : threads_ / cores_;
    }

    // ── L3 grouping ─────────────────────────────────────────────────
    //
    // Returns spans of cpu IDs sharing each L3 instance.  Outer span
    // has one entry per L3 instance (= socket).  Inner spans contain
    // the SMT thread IDs (not just physical cores) sharing that L3.

    [[nodiscard]] std::span<const std::vector<int>> l3_groups() const noexcept {
        return {l3_groups_.data(), l3_groups_.size()};
    }

    // Largest L3-sharing group's size — proxy for cores-per-socket.
    [[nodiscard]] std::size_t cores_per_socket() const noexcept {
        std::size_t m = 0;
        for (auto const& g : l3_groups_) m = std::max(m, g.size());
        return m == 0 ? cores_ : m;
    }

    // ── NUMA topology ───────────────────────────────────────────────

    [[nodiscard]] std::size_t numa_nodes() const noexcept {
        return cores_on_node_.size();
    }

    [[nodiscard]] int numa_distance(int from, int to) const noexcept {
        if (from < 0 || to < 0) return 10;
        const std::size_t f = static_cast<std::size_t>(from);
        const std::size_t t = static_cast<std::size_t>(to);
        if (f >= numa_distance_.size()) return 10;
        if (t >= numa_distance_[f].size()) return 10;
        return numa_distance_[f][t];
    }

    [[nodiscard]] std::span<const int> cores_on_node(int node) const noexcept {
        if (node < 0) return {};
        const std::size_t n = static_cast<std::size_t>(node);
        if (n >= cores_on_node_.size()) return {};
        return {cores_on_node_[n].data(), cores_on_node_[n].size()};
    }

    // ── Probe origin diagnostic ─────────────────────────────────────

    [[nodiscard]] Source source() const noexcept { return source_; }

private:
    // Fallback defaults — chosen conservatively.  Underestimating
    // cache sizes biases the cost model toward parallelism, which is
    // the safer error mode (over-parallel regresses ~2x; under-
    // parallel loses ~Nx on DRAM-bound workloads).
    static constexpr std::size_t kFallbackL1d  = 32 * 1024;
    static constexpr std::size_t kFallbackL2   = 1024 * 1024;
    static constexpr std::size_t kFallbackL3   = 32ULL * 1024 * 1024;
    static constexpr std::size_t kFallbackLine = 64;

    std::size_t                   l1d_     = kFallbackL1d;
    std::size_t                   l2_      = kFallbackL2;
    std::size_t                   l3_      = kFallbackL3;
    std::size_t                   line_    = kFallbackLine;
    std::size_t                   cores_   = 1;
    std::size_t                   threads_ = 1;
    std::vector<std::vector<int>> l3_groups_;
    std::vector<std::vector<int>> cores_on_node_;
    std::vector<std::vector<int>> numa_distance_;
    Source                        source_  = Source::Fallback;

    // Constructor probes Linux sysfs; falls back to defaults on
    // any failure.  Marked noexcept — we never throw out, even on
    // catastrophic /sys corruption (graceful fallback).
    Topology() noexcept {
        // Always-available baseline: hardware_concurrency.
        const unsigned hw = std::thread::hardware_concurrency();
        cores_   = (hw == 0) ? 1 : hw;
        threads_ = cores_;

        // Fallback NUMA: single node, self-distance 10.
        cores_on_node_.push_back({});
        numa_distance_.push_back({10});
        for (std::size_t i = 0; i < cores_; ++i) {
            cores_on_node_[0].push_back(static_cast<int>(i));
        }

#if __has_include(<filesystem>)
        probe_linux_();
#endif
    }

#if __has_include(<filesystem>)
    void probe_linux_() noexcept;
#endif
};

// ── Linux probe implementation (out-of-line for readability) ────────

#if __has_include(<filesystem>)
inline void Topology::probe_linux_() noexcept {
    using namespace topology_detail;

    const auto cpus = enumerate_cpus_();
    if (cpus.empty()) return;  // no /sys; keep fallback

    // CPU count from sysfs (more authoritative than hardware_concurrency
    // when cgroup or hot-unplug applies — but only if it's positive).
    threads_ = cpus.size();

    // ── Cache hierarchy: read cpu0's caches ─────────────────────────
    //
    // Assume all cores have homogeneous cache hierarchy (true on every
    // x86-64 / aarch64 platform we care about).  Asymmetric caches
    // (e.g. Apple Silicon E vs P cores) would need per-core probing;
    // defer until we ship on Apple Silicon.

    {
        const std::string base = "/sys/devices/system/cpu/cpu" +
                                  std::to_string(cpus[0]) + "/cache";
        std::set<int> physical_cores;  // dedup via core_id

        // Walk index0..indexN
        namespace fs = std::filesystem;
        try {
            if (!fs::exists(base)) return;
            for (auto const& entry : fs::directory_iterator{base}) {
                const auto name = entry.path().filename().string();
                if (name.size() < 6 || name.substr(0, 5) != "index") continue;

                const std::string idx_dir = base + "/" + name;
                const auto level_str = read_trimmed_(idx_dir + "/level");
                const auto type_str  = read_trimmed_(idx_dir + "/type");
                const auto size_str  = read_trimmed_(idx_dir + "/size");
                const auto line_str  = read_trimmed_(
                    idx_dir + "/coherency_line_size");

                int level = 0;
                std::from_chars(level_str.data(),
                                level_str.data() + level_str.size(), level);
                const std::size_t size = parse_size_suffix_(size_str);
                std::size_t line = 0;
                std::from_chars(line_str.data(),
                                line_str.data() + line_str.size(), line);

                if (line > 0) line_ = line;
                if (size == 0) continue;

                if (level == 1) {
                    // L1 has separate Data and Instruction; only
                    // record Data.  Type is "Data" or "Instruction"
                    // (or rarely "Unified" on some uarchs — accept).
                    if (type_str == "Data" || type_str == "Unified") {
                        l1d_ = size;
                    }
                } else if (level == 2) {
                    l2_ = size;
                } else if (level == 3) {
                    l3_ = size;
                }
            }
        } catch (...) {
            // Cache probe failed; keep fallback.
        }
    }

    // ── Physical core count via core_id ─────────────────────────────
    //
    // SMT siblings share a core_id.  Counting unique core_ids gives
    // the physical core count (vs threads_ which is logical thread
    // count).

    {
        std::set<int> physical_core_ids;
        for (int cpu : cpus) {
            const std::string p = "/sys/devices/system/cpu/cpu" +
                                   std::to_string(cpu) + "/topology/core_id";
            const auto s = read_trimmed_(p);
            int core_id = -1;
            std::from_chars(s.data(), s.data() + s.size(), core_id);
            if (core_id >= 0) physical_core_ids.insert(core_id);
        }
        if (!physical_core_ids.empty()) {
            cores_ = physical_core_ids.size();
        }
    }

    // ── L3 groups via shared_cpu_list on the L3 cache index ─────────
    //
    // For each cpu, find its L3 cache's shared_cpu_list (the set of
    // cpus sharing that L3).  Deduplicate by sorting + comparing.

    {
        std::vector<std::vector<int>> all_groups;
        std::set<std::vector<int>> seen;
        namespace fs = std::filesystem;
        for (int cpu : cpus) {
            const std::string base = "/sys/devices/system/cpu/cpu" +
                                      std::to_string(cpu) + "/cache";
            try {
                if (!fs::exists(base)) continue;
                for (auto const& entry : fs::directory_iterator{base}) {
                    const auto name = entry.path().filename().string();
                    if (name.size() < 6 || name.substr(0, 5) != "index") continue;

                    const std::string idx_dir = base + "/" + name;
                    const auto level_str = read_trimmed_(idx_dir + "/level");
                    int level = 0;
                    std::from_chars(level_str.data(),
                                    level_str.data() + level_str.size(),
                                    level);
                    if (level != 3) continue;

                    auto group = parse_cpu_list_(
                        read_trimmed_(idx_dir + "/shared_cpu_list"));
                    std::sort(group.begin(), group.end());
                    if (group.empty()) continue;
                    if (seen.insert(group).second) {
                        all_groups.push_back(std::move(group));
                    }
                    break;  // only need cpu's L3 (one per cpu)
                }
            } catch (...) {
                // skip this cpu
            }
        }
        if (!all_groups.empty()) {
            l3_groups_ = std::move(all_groups);
        }
    }

    // ── NUMA topology ───────────────────────────────────────────────
    //
    // Read each node's cpulist + distance row.  Replace fallback
    // single-node only if probe yields >= 1 node.

    {
        const auto nodes = enumerate_numa_nodes_();
        if (!nodes.empty()) {
            std::vector<std::vector<int>> new_cores_on_node;
            std::vector<std::vector<int>> new_numa_distance;

            for (int node : nodes) {
                const std::string base = "/sys/devices/system/node/node" +
                                          std::to_string(node);
                auto cpus_on_node = parse_cpu_list_(
                    read_trimmed_(base + "/cpulist"));
                std::sort(cpus_on_node.begin(), cpus_on_node.end());
                new_cores_on_node.push_back(std::move(cpus_on_node));

                auto dist_row = parse_int_list_(
                    read_trimmed_(base + "/distance"));
                if (dist_row.empty()) {
                    // Fallback row: self-distance only.
                    dist_row.assign(nodes.size(), 20);
                    if (static_cast<std::size_t>(node) < dist_row.size()) {
                        dist_row[static_cast<std::size_t>(node)] = 10;
                    }
                }
                new_numa_distance.push_back(std::move(dist_row));
            }

            if (!new_cores_on_node.empty()) {
                cores_on_node_ = std::move(new_cores_on_node);
                numa_distance_ = std::move(new_numa_distance);
            }
        }
    }

    source_ = Source::Sysfs;
}
#endif  // __has_include(<filesystem>)

// ── Compile-time sanity ─────────────────────────────────────────────

static_assert(std::is_class_v<Topology>);
// Pinned (deleted move/copy) — singleton's address is the identity.
static_assert(!std::is_copy_constructible_v<Topology>);
static_assert(!std::is_move_constructible_v<Topology>);

}  // namespace crucible::concurrent
