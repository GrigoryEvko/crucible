#pragma once

// CPU / NUMA topology discovery for the Crucible realtime runtime.
//
// Sysfs-only. No hwloc dep. ~150 lines of parsing to replicate the
// subset hwloc has that Crucible actually needs: cpuset membership,
// isolcpu list, Intel-hybrid core type, SMT siblings, NUMA node of a
// CPU. Everything is cached once per process — callers can hit these
// in hot paths without repeated sysfs opens.
//
// All queries return empty / sentinel on read failure rather than
// throwing. The Keeper / bench consumer degrades gracefully if the
// kernel doesn't expose a particular file (older kernels, chroots,
// non-standard filesystems).

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#ifdef __linux__
#  include <sched.h>
#  include <unistd.h>
#endif

namespace crucible::rt {

// ── Internal sysfs helpers ─────────────────────────────────────────

namespace detail {

[[nodiscard]] inline std::string read_small_file(const char* path) noexcept {
    std::string out;
    std::FILE* f = std::fopen(path, "r");
    if (f == nullptr) return out;
    char buf[512];
    while (std::fgets(buf, sizeof(buf), f)) out.append(buf);
    std::fclose(f);
    // strip trailing newline(s)
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
    return out;
}

// Parse a Linux "cpulist" string — e.g. "0-3,5,7-9,15" — into ints.
[[nodiscard]] inline std::vector<int> parse_cpulist(std::string_view s) noexcept {
    std::vector<int> out;
    size_t i = 0;
    while (i < s.size()) {
        // skip whitespace/commas
        while (i < s.size() && (s[i] == ',' || s[i] == ' ' || s[i] == '\t')) ++i;
        if (i >= s.size()) break;
        const size_t num_start = i;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9') ++i;
        if (i == num_start) { ++i; continue; }
        const int lo = std::atoi(s.data() + num_start);
        int hi = lo;
        if (i < s.size() && s[i] == '-') {
            ++i;
            const size_t hi_start = i;
            while (i < s.size() && s[i] >= '0' && s[i] <= '9') ++i;
            if (i > hi_start) hi = std::atoi(s.data() + hi_start);
        }
        for (int c = lo; c <= hi; ++c) out.push_back(c);
    }
    // dedup + sort
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

// Parse the "Cpus_allowed_list:\t0-27" line out of /proc/self/status.
[[nodiscard]] inline std::vector<int> parse_cpus_allowed_list(std::string_view status) noexcept {
    constexpr std::string_view key = "Cpus_allowed_list:";
    const auto pos = status.find(key);
    if (pos == std::string_view::npos) return {};
    auto rest = status.substr(pos + key.size());
    const auto nl = rest.find('\n');
    if (nl != std::string_view::npos) rest = rest.substr(0, nl);
    return parse_cpulist(rest);
}

} // namespace detail

// ── Public API ─────────────────────────────────────────────────────

// Total number of online CPUs. Returns 1 on failure (conservative).
[[nodiscard]] inline int num_online_cpus() noexcept {
#ifdef __linux__
    const long n = sysconf(_SC_NPROCESSORS_ONLN);
    return (n > 0) ? static_cast<int>(n) : 1;
#else
    return 1;
#endif
}

// CPUs granted to this task by the orchestrator / cpuset cgroup.
// Sourced from /proc/self/status:Cpus_allowed_list (authoritative).
// Falls back to the kernel mask via sched_getaffinity if procfs fails.
[[nodiscard]] inline std::vector<int> allowed_cpus() noexcept {
    const auto status = detail::read_small_file("/proc/self/status");
    if (auto v = detail::parse_cpus_allowed_list(status); !v.empty()) return v;

#ifdef __linux__
    cpu_set_t set;
    CPU_ZERO(&set);
    if (sched_getaffinity(0, sizeof(set), &set) == 0) {
        std::vector<int> out;
        out.reserve(static_cast<size_t>(CPU_COUNT(&set)));
        for (int c = 0; c < CPU_SETSIZE; ++c)
            if (CPU_ISSET(static_cast<size_t>(c), &set)) out.push_back(c);
        return out;
    }
#endif
    // Last-resort: assume all online are allowed.
    std::vector<int> out;
    const int n = num_online_cpus();
    out.reserve(static_cast<size_t>(n));
    for (int c = 0; c < n; ++c) out.push_back(c);
    return out;
}

// CPUs quarantined by the kernel cmdline `isolcpus=…`. Empty if unset.
[[nodiscard]] inline std::vector<int> isolated_cpus() noexcept {
    return detail::parse_cpulist(detail::read_small_file("/sys/devices/system/cpu/isolated"));
}

// /sys/devices/system/cpu/cpuN/topology/thread_siblings_list
[[nodiscard]] inline std::vector<int> smt_siblings(int cpu) noexcept {
    char path[128];
    std::snprintf(path, sizeof(path),
        "/sys/devices/system/cpu/cpu%d/topology/thread_siblings_list", cpu);
    return detail::parse_cpulist(detail::read_small_file(path));
}

// Intel hybrid: the topology/core_type file reports "Core" (P-core) or
// "Atom" (E-core). Non-hybrid CPUs don't expose the file; we treat the
// absence as "Core" so homogeneous hosts default to true.
[[nodiscard]] inline bool is_p_core(int cpu) noexcept {
    char path[128];
    std::snprintf(path, sizeof(path),
        "/sys/devices/system/cpu/cpu%d/topology/core_type", cpu);
    const auto s = detail::read_small_file(path);
    if (s.empty()) return true;
    return s.find("Core") != std::string::npos;
}

// NUMA node of a CPU. -1 on failure. /sys/devices/system/cpu/cpuN/node<M>
// is a symlink; simpler to scan /sys/devices/system/node/*/cpulist.
[[nodiscard]] inline int numa_node_of(int cpu) noexcept {
    // Try /sys/devices/system/cpu/cpuN/node<M> symlink first (cheaper).
    for (int n = 0; n < 64; ++n) {
        char path[128];
        std::snprintf(path, sizeof(path),
            "/sys/devices/system/cpu/cpu%d/node%d", cpu, n);
        if (::access(path, F_OK) == 0) return n;
    }
    return -1;
}

// NUMA node closest to a given PCI device (GPU, NIC). Pass a sysfs
// path ending in .../numa_node. -1 on failure.
[[nodiscard]] inline int numa_node_of_device(const char* sysfs_numa_node_path) noexcept {
    const auto s = detail::read_small_file(sysfs_numa_node_path);
    if (s.empty()) return -1;
    const int n = std::atoi(s.c_str());
    return n < 0 ? -1 : n;
}

// Current CPU frequency (kHz) from cpufreq sysfs. 0 on failure.
[[nodiscard]] inline uint64_t cpu_cur_freq_khz(int cpu) noexcept {
    char path[128];
    std::snprintf(path, sizeof(path),
        "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_cur_freq", cpu);
    const auto s = detail::read_small_file(path);
    if (s.empty()) return 0;
    const long v = std::atol(s.c_str());
    return v > 0 ? static_cast<uint64_t>(v) : 0;
}

// Max achievable frequency (kHz). 0 on failure.
[[nodiscard]] inline uint64_t cpu_max_freq_khz(int cpu) noexcept {
    char path[128];
    std::snprintf(path, sizeof(path),
        "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_max_freq", cpu);
    const auto s = detail::read_small_file(path);
    if (s.empty()) return 0;
    const long v = std::atol(s.c_str());
    return v > 0 ? static_cast<uint64_t>(v) : 0;
}

// ── Core selection for the HOT thread ──────────────────────────────

struct CoreSelector {
    bool prefer_isolcpu    = true;   // pick from (allowed ∩ isolated) first
    bool prefer_p_core     = true;   // Intel hybrid: avoid E-cores
    bool avoid_smt_sibling = true;   // skip siblings of already-pinned cores
    int  explicit_cpu      = -1;     // if >=0, short-circuit to this CPU iff allowed

    // NUMA hint: if >=0, prefer cores on this node. -1 = no preference.
    // Typical source: numa_node_of_device("/sys/class/drm/card0/device/numa_node").
    int  numa_hint         = -1;
};

// Pick one CPU for the HOT dispatch thread. Returns -1 if the current
// cgroup has no CPUs at all (catastrophic; caller should refuse to
// start the Keeper).
[[nodiscard]] inline int select_hot_cpu(const CoreSelector& sel,
                                        const std::vector<int>& exclude = {}) noexcept {
    const auto allowed = allowed_cpus();
    if (allowed.empty()) return -1;

    // Explicit override (bench benchmarks / manual pinning).
    if (sel.explicit_cpu >= 0 &&
        std::find(allowed.begin(), allowed.end(), sel.explicit_cpu) != allowed.end()) {
        return sel.explicit_cpu;
    }

    const auto is_excluded = [&](int c) {
        return std::find(exclude.begin(), exclude.end(), c) != exclude.end();
    };

    // Build candidate pools in preference order.
    std::vector<std::vector<int>> pools;

    // Pool 1: allowed ∩ isolcpus (if prefer_isolcpu).
    if (sel.prefer_isolcpu) {
        std::vector<int> iso = isolated_cpus();
        std::vector<int> inter;
        std::set_intersection(allowed.begin(), allowed.end(),
                              iso.begin(), iso.end(),
                              std::back_inserter(inter));
        if (!inter.empty()) pools.push_back(std::move(inter));
    }
    // Pool 2: all allowed (fallback).
    pools.push_back(allowed);

    auto try_pool = [&](const std::vector<int>& pool) -> int {
        // Rank by (p_core, numa_match, not_smt_sibling) with reasonable weights.
        int best = -1;
        int best_score = -1;
        for (const int c : pool) {
            if (is_excluded(c)) continue;
            // Avoid SMT siblings of already-selected cores.
            if (sel.avoid_smt_sibling) {
                const auto sibs = smt_siblings(c);
                bool clash = false;
                for (const int s : sibs)
                    if (s != c && is_excluded(s)) { clash = true; break; }
                if (clash) continue;
            }
            int score = 0;
            if (sel.prefer_p_core && is_p_core(c))          score += 4;
            if (sel.numa_hint >= 0 &&
                numa_node_of(c) == sel.numa_hint)           score += 2;
            // Lower CPU index = tie-break (stable, predictable).
            score = score * 1024 - c;
            if (score > best_score) { best_score = score; best = c; }
        }
        return best;
    };

    for (const auto& pool : pools) {
        const int pick = try_pool(pool);
        if (pick >= 0) return pick;
    }
    // Nothing matched the scoring criteria — return any allowed CPU
    // that isn't in `exclude`.
    for (const int c : allowed) if (!is_excluded(c)) return c;
    return -1;
}

// Pick a set of CPUs for WARM threads (same NUMA node as hot, NOT
// including the hot CPU itself). Empty on failure.
[[nodiscard]] inline std::vector<int> select_warm_cpus(int hot_cpu,
                                                       int count) noexcept {
    const auto allowed = allowed_cpus();
    const int hot_numa = (hot_cpu >= 0) ? numa_node_of(hot_cpu) : -1;

    std::vector<int> same_numa;
    std::vector<int> other;
    for (const int c : allowed) {
        if (c == hot_cpu) continue;
        const int n = numa_node_of(c);
        if (hot_numa >= 0 && n == hot_numa) same_numa.push_back(c);
        else other.push_back(c);
    }
    std::vector<int> out;
    out.reserve(static_cast<size_t>(count));
    for (const int c : same_numa) { if (static_cast<int>(out.size()) >= count) break; out.push_back(c); }
    for (const int c : other)     { if (static_cast<int>(out.size()) >= count) break; out.push_back(c); }
    return out;
}

} // namespace crucible::rt
