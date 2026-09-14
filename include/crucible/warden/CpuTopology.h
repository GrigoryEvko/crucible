#pragma once

// CPU and NUMA placement read straight out of sysfs and procfs. A
// general topology library would cover far more than the handful of
// facts needed here, so the few files are parsed directly instead.
//
// A query that cannot read what it needs returns an empty list or a
// sentinel rather than failing, so an older kernel, a chroot or an
// unusual filesystem costs a degraded answer and nothing worse.
//
// The queries are marked pure even though each one opens a file, which
// lets the compiler fold repeated calls into one. A caller that needs
// a fresh reading must put other work between two calls.

#include <crucible/safety/Decide.h>
#include <crucible/fixy/Handle.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#ifdef __linux__
#include <sched.h>
#include <unistd.h>
#endif

namespace crucible::warden {

// The tests reach into this namespace to exercise the parser on its
// own, so these names are part of the tested surface.

namespace detail {

[[nodiscard, gnu::pure]] inline std::string read_small_file(const char* path) noexcept {
    std::string out;
    // The close is discharged by the handle on every exit path,
    // including the early return below. Its result is ignored because
    // a failure to close a sysfs read offers nothing to act on.
    ::crucible::fixy::handle::OwnedFile f{std::fopen(path, "r")};
    if (!f.is_open()) return out;
    char buf[512];
    while (std::fgets(buf, sizeof(buf), f.get()))
        out.append(buf);
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r'))
        out.pop_back();
    return out;
}

// A kernel CPU list names single CPUs and inclusive ranges, separated
// by commas, as in "0-3,5,7-9,15".
[[nodiscard, gnu::pure]] inline std::vector<int> parse_cpulist(std::string_view s) noexcept {
    std::vector<int> out;
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && (s[i] == ',' || s[i] == ' ' || s[i] == '\t'))
            ++i;
        if (i >= s.size()) break;
        const size_t num_start = i;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9')
            ++i;
        if (i == num_start) {
            ++i;
            continue;
        }
        const int lo = std::atoi(s.data() + num_start);
        int hi = lo;
        if (i < s.size() && s[i] == '-') {
            ++i;
            const size_t hi_start = i;
            while (i < s.size() && s[i] >= '0' && s[i] <= '9')
                ++i;
            if (i > hi_start) hi = std::atoi(s.data() + hi_start);
        }
        for (int c = lo; c <= hi; ++c)
            out.push_back(c);
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

// /proc/self/status carries one line per field. The line of interest
// reads "Cpus_allowed_list:" followed by a tab and a CPU list.
[[nodiscard, gnu::pure]] inline std::vector<int> parse_cpus_allowed_list(std::string_view status) noexcept {
    constexpr std::string_view key = "Cpus_allowed_list:";
    const auto pos = status.find(key);
    if (pos == std::string_view::npos) return {};
    auto rest = status.substr(pos + key.size());
    const auto nl = rest.find('\n');
    if (nl != std::string_view::npos) rest = rest.substr(0, nl);
    return parse_cpulist(rest);
}

}  // namespace detail

// glibc has fixed this at 1024 ever since the fixed-size CPU set macros
// shipped. A host with more CPUs than that needs the dynamic-set calls,
// which nothing here uses. A smaller value on some other library would
// make the fallback below truncate in silence, so it is caught here.
#ifdef __linux__
static_assert(CPU_SETSIZE >= 1024, "The allowed_cpus fallback iterates a fixed-size CPU set and assumes it "
                                   "holds at least 1024 CPUs.");
#endif

// One on failure, which is the conservative answer.
[[nodiscard, gnu::pure]] inline int num_online_cpus() noexcept {
#ifdef __linux__
    const long n = sysconf(_SC_NPROCESSORS_ONLN);
    return (n > 0) ? static_cast<int>(n) : 1;
#else
    return 1;
#endif
}

namespace detail {

// Reached when procfs is unreadable or malformed. Ask the kernel
// directly, and if even that fails assume every online CPU is
// available.
[[nodiscard, gnu::cold]] inline std::vector<int> allowed_cpus_fallback() noexcept {
#ifdef __linux__
    cpu_set_t set;
    CPU_ZERO(&set);
    if (::sched_getaffinity(0, sizeof(set), &set) == 0) {
        std::vector<int> out;
        for (int c = 0; c < CPU_SETSIZE; ++c)
            if (CPU_ISSET(static_cast<size_t>(c), &set)) out.push_back(c);
        return out;
    }
#endif
    std::vector<int> out;
    const int n = num_online_cpus();
    for (int c = 0; c < n; ++c)
        out.push_back(c);
    return out;
}

}  // namespace detail

// The CPUs this task may run on, as granted by whatever placed it.
// Procfs is the authoritative source and is consulted first.
[[nodiscard, gnu::pure]] inline std::vector<int> allowed_cpus() noexcept {
    const auto status = detail::read_small_file("/proc/self/status");
    if (auto v = detail::parse_cpus_allowed_list(status); !v.empty()) return v;
    return detail::allowed_cpus_fallback();
}

// The CPUs the kernel command line has withheld from the scheduler.
// Empty when no such argument was given.
[[nodiscard, gnu::pure]] inline std::vector<int> isolated_cpus() noexcept {
    return detail::parse_cpulist(detail::read_small_file("/sys/devices/system/cpu/isolated"));
}

[[nodiscard, gnu::pure]] inline std::vector<int> smt_siblings(int cpu) noexcept {
    char path[128];
    std::snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/topology/thread_siblings_list", cpu);
    return detail::parse_cpulist(detail::read_small_file(path));
}

// On a hybrid part the core-type file reads "Core" for a performance
// core and "Atom" for an efficiency core. A uniform part does not
// publish the file at all, and its absence counts as a performance
// core so that every CPU on such a host answers the same way.
[[nodiscard, gnu::pure]] inline bool is_p_core(int cpu) noexcept {
    char path[128];
    std::snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/topology/core_type", cpu);
    const auto s = detail::read_small_file(path);
    if (s.empty()) return true;
    return s.find("Core") != std::string::npos;
}

// The NUMA node a CPU belongs to, or -1 when it cannot be determined.
// The kernel publishes the membership as a symlink named after the
// node, so the node number is recovered by testing which name exists.
// Nodes past the search bound below read as -1.
[[nodiscard, gnu::pure]] inline int numa_node_of(int cpu) noexcept {
    for (int n = 0; n < 64; ++n) {
        char path[128];
        std::snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/node%d", cpu, n);
        if (::access(path, F_OK) == 0) return n;
    }
    return -1;
}

// The NUMA node nearest a device. The caller passes the sysfs path of
// that device's numa_node file. -1 when it cannot be read.
[[nodiscard, gnu::pure]] inline int numa_node_of_device(const char* sysfs_numa_node_path) noexcept {
    const auto s = detail::read_small_file(sysfs_numa_node_path);
    if (s.empty()) return -1;
    const int n = std::atoi(s.c_str());
    return n < 0 ? -1 : n;
}

// Zero when the frequency driver publishes nothing for this CPU.
[[nodiscard, gnu::pure]] inline uint64_t cpu_cur_freq_khz(int cpu) noexcept {
    char path[128];
    std::snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_cur_freq", cpu);
    const auto s = detail::read_small_file(path);
    if (s.empty()) return 0;
    const long v = std::atol(s.c_str());
    return v > 0 ? static_cast<uint64_t>(v) : 0;
}

// Zero when the frequency driver publishes nothing for this CPU.
[[nodiscard, gnu::pure]] inline uint64_t cpu_max_freq_khz(int cpu) noexcept {
    char path[128];
    std::snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_max_freq", cpu);
    const auto s = detail::read_small_file(path);
    if (s.empty()) return 0;
    const long v = std::atol(s.c_str());
    return v > 0 ? static_cast<uint64_t>(v) : 0;
}

struct CoreSelector {
    bool prefer_isolcpu = true;
    bool prefer_p_core = true;
    bool avoid_smt_sibling = true;
    int explicit_cpu = -1;  // taken only if it is also allowed

    // The first CPU is where the timer interrupt lands by default,
    // where a newly registered interrupt is first steered, and where
    // deferred kernel callbacks run unless the command line moved them
    // elsewhere. Its hyperthread sibling shares the low-level caches
    // with that work. Both are noisier than the rest of the machine.
    //
    // This is a preference and not an exclusion: the last-resort branch
    // of select_hot_cpu still returns the first CPU when it is the only
    // one available.
    bool avoid_cpu0 = true;

    int numa_hint = -1;  // -1 expresses no preference
};

// Returns -1 only when this task is allowed no CPU at all, which is
// unrecoverable and means the caller should refuse to start.
[[nodiscard]] inline int select_hot_cpu(const CoreSelector& sel, const std::vector<int>& exclude = {}) noexcept {
    // The bonuses are multiplied by this before the CPU index is
    // subtracted, so the index can only break ties between equal
    // bonuses and never outweigh one. 1024 exceeds the size of the
    // fixed CPU set this selector works with.
    constexpr int kScoreScale = 1024;

    const auto allowed = allowed_cpus();
    if (allowed.empty()) return -1;

    if (sel.explicit_cpu >= 0 && std::find(allowed.begin(), allowed.end(), sel.explicit_cpu) != allowed.end()) {
        return sel.explicit_cpu;
    }

    const auto is_excluded = [&](int c) noexcept {
        return std::find(exclude.begin(), exclude.end(), c) != exclude.end();
    };

    // Candidate pools, most preferred first.
    std::vector<std::vector<int>> pools;

    if (sel.prefer_isolcpu) {
        std::vector<int> iso = isolated_cpus();
        std::vector<int> inter;
        std::set_intersection(allowed.begin(), allowed.end(), iso.begin(), iso.end(), std::back_inserter(inter));
        if (!inter.empty()) pools.push_back(std::move(inter));
    }
    pools.push_back(allowed);

    auto try_pool = [&](const std::vector<int>& pool) noexcept -> int {
        // The starting score is the lowest representable one so that a
        // candidate scoring below zero, which the first CPU does under
        // its penalty, is still chosen when it is the only one here.
        int best = -1;
        int best_score = std::numeric_limits<int>::min();
        for (const int c : pool) {
            if (is_excluded(c)) continue;
            if (sel.avoid_smt_sibling) {
                const auto sibs = smt_siblings(c);
                bool clash = false;
                for (const int s : sibs)
                    if (s != c && is_excluded(s)) {
                        clash = true;
                        break;
                    }
                if (clash) continue;
            }
            int score = 0;
            if (sel.prefer_p_core && is_p_core(c)) score += 4;
            if (sel.numa_hint >= 0 && numa_node_of(c) == sel.numa_hint) score += 2;
            // The penalties are sized against the performance-core
            // bonus above. The larger one outweighs it, so any other
            // CPU beats the first one. The smaller one cancels it, so
            // the sibling loses to an unrelated performance core but
            // still beats an unrelated efficiency core.
            if (sel.avoid_cpu0) {
                if (c == 0) {
                    score -= 8;
                } else {
                    const auto sibs = smt_siblings(c);
                    if (std::find(sibs.begin(), sibs.end(), 0) != sibs.end()) score -= 4;
                }
            }
            // Between equal scores the lowest index wins, which keeps
            // the choice stable from run to run.
            score = score * kScoreScale - c;
            if (score > best_score) {
                best_score = score;
                best = c;
            }
        }
        return best;
    };

    for (const auto& pool : pools) {
        const int pick = try_pool(pool);
        if (pick >= 0) return pick;
    }
    for (const int c : allowed)
        if (!is_excluded(c)) return c;
    return -1;
}

// Companion CPUs for the supporting threads. Those on the same NUMA
// node as the given CPU come first, and the given CPU itself is never
// among them.
[[nodiscard]] inline std::vector<int> select_warm_cpus(int hot_cpu, int count) noexcept
    pre(::crucible::decide::non_negative(count)) {
    const auto allowed = allowed_cpus();
    const int hot_numa = (hot_cpu >= 0) ? numa_node_of(hot_cpu) : -1;

    std::vector<int> same_numa;
    std::vector<int> other;
    for (const int c : allowed) {
        if (c == hot_cpu) continue;
        const int n = numa_node_of(c);
        if (hot_numa >= 0 && n == hot_numa)
            same_numa.push_back(c);
        else
            other.push_back(c);
    }
    std::vector<int> out;
    for (const int c : same_numa) {
        if (static_cast<int>(out.size()) >= count) break;
        out.push_back(c);
    }
    for (const int c : other) {
        if (static_cast<int>(out.size()) >= count) break;
        out.push_back(c);
    }
    return out;
}

}  // namespace crucible::warden
