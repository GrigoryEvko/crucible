#pragma once

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

#if __has_include(<sched.h>)
#include <sched.h>
#define CRUCIBLE_HAS_SCHED_AFFINITY 1
#else
#define CRUCIBLE_HAS_SCHED_AFFINITY 0
#endif

#if __has_include(<unistd.h>)
#include <unistd.h>
#define CRUCIBLE_HAS_UNISTD 1
#else
#define CRUCIBLE_HAS_UNISTD 0
#endif

namespace crucible::concurrent {

namespace topology_detail {

// Empty on any failure to read.  The trailing newline every sysfs file ends
// with is trimmed off.
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
        while (
            !content.empty()
            && (content.back() == '\n' || content.back() == ' ' || content.back() == '\t' || content.back() == '\r')) {
            content.pop_back();
        }
        return content;
    } catch (...) {
        return {};
    }
}

// Sysfs writes cache sizes with a K, M or G suffix, or none at all.  Zero on
// a parse failure.
[[nodiscard]] inline std::size_t parse_size_suffix_(std::string_view s) noexcept {
    if (s.empty()) return 0;
    char suffix = '\0';
    std::string_view digits = s;
    if (s.back() == 'K' || s.back() == 'M' || s.back() == 'G' || s.back() == 'k' || s.back() == 'm'
        || s.back() == 'g') {
        suffix = static_cast<char>(s.back() | 0x20);  // tolower
        digits = s.substr(0, s.size() - 1);
    }
    std::size_t value = 0;
    auto [ptr, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), value);
    if (ec != std::errc{}) return 0;
    switch (suffix) {
        case 'k':
            return value * 1024;
        case 'm':
            return value * 1024 * 1024;
        case 'g':
            return value * 1024 * 1024 * 1024;
        default:
            return value;
    }
}

// Sysfs writes CPU sets as comma-separated singletons and inclusive ranges,
// as in "0-3,8-11,16".  Empty on a parse failure.
[[nodiscard]] inline std::vector<int> parse_cpu_list_(std::string_view s) noexcept {
    std::vector<int> result;
    try {
        std::size_t pos = 0;
        while (pos < s.size()) {
            while (pos < s.size() && (s[pos] == ' ' || s[pos] == ','))
                ++pos;
            if (pos >= s.size()) break;

            int start = 0;
            auto [p1, ec1] = std::from_chars(s.data() + pos, s.data() + s.size(), start);
            if (ec1 != std::errc{}) return {};
            pos = static_cast<std::size_t>(p1 - s.data());

            int end = start;
            if (pos < s.size() && s[pos] == '-') {
                ++pos;
                auto [p2, ec2] = std::from_chars(s.data() + pos, s.data() + s.size(), end);
                if (ec2 != std::errc{}) return {};
                pos = static_cast<std::size_t>(p2 - s.data());
            }
            for (int i = start; i <= end; ++i)
                result.push_back(i);
        }
    } catch (...) {
        return {};
    }
    return result;
}

// One row of the NUMA distance matrix: integers separated by spaces.
[[nodiscard]] inline std::vector<int> parse_int_list_(std::string_view s) noexcept {
    std::vector<int> result;
    try {
        std::size_t pos = 0;
        while (pos < s.size()) {
            while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t'))
                ++pos;
            if (pos >= s.size()) break;
            int v = 0;
            auto [p, ec] = std::from_chars(s.data() + pos, s.data() + s.size(), v);
            if (ec != std::errc{}) return {};
            pos = static_cast<std::size_t>(p - s.data());
            result.push_back(v);
        }
    } catch (...) {
        return {};
    }
    return result;
}

// The CPU directory holds entries such as "cpufreq" and "cpuidle" alongside
// the per-CPU ones, so only a "cpu" followed by digits alone counts.
[[nodiscard]] inline std::vector<int> enumerate_cpus_() noexcept {
    std::vector<int> cpus;
#if __has_include(<filesystem>)
    namespace fs = std::filesystem;
    try {
        if (!fs::exists("/sys/devices/system/cpu")) return cpus;
        for (auto const& entry : fs::directory_iterator{"/sys/devices/system/cpu"}) {
            const auto name = entry.path().filename().string();
            if (name.size() < 4 || name.substr(0, 3) != "cpu") continue;
            const std::string_view tail{name.data() + 3, name.size() - 3};
            int id = 0;
            auto [p, ec] = std::from_chars(tail.data(), tail.data() + tail.size(), id);
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

// Zero where the affinity call does not exist, which callers read as "ask the
// standard library instead".
[[nodiscard]] inline std::size_t probe_process_cpu_count_() noexcept {
#if CRUCIBLE_HAS_SCHED_AFFINITY
    cpu_set_t mask;
    CPU_ZERO(&mask);
    if (sched_getaffinity(0, sizeof(mask), &mask) == 0) {
        return static_cast<std::size_t>(CPU_COUNT(&mask));
    }
#endif
    return 0;
}

[[nodiscard]] inline std::size_t probe_page_size_() noexcept {
#if CRUCIBLE_HAS_UNISTD
    const long s = sysconf(_SC_PAGESIZE);
    return (s > 0) ? static_cast<std::size_t>(s) : 4096;
#else
    return 4096;
#endif
}

// The kernel writes all three settings and brackets the active one, as in
// "always [madvise] never".  Either "always" or "madvise" counts as available,
// the second requiring the allocation to ask for it.
[[nodiscard]] inline bool probe_hugepage_2mb_available_() noexcept {
    const auto contents = read_trimmed_("/sys/kernel/mm/transparent_hugepage/enabled");
    if (contents.empty()) return false;
    return contents.find("[always]") != std::string::npos || contents.find("[madvise]") != std::string::npos;
}

// Only the first entry is read.  This assumes every CPU in the machine is the
// same model.
[[nodiscard]] inline std::pair<std::string, std::string> probe_cpu_vendor_and_model_() noexcept {
    std::string vendor, model;
    try {
        std::ifstream f{"/proc/cpuinfo"};
        if (!f.is_open()) return {vendor, model};
        std::string line;
        while (std::getline(f, line)) {
            if (vendor.empty() && line.starts_with("vendor_id")) {
                const auto pos = line.find(':');
                if (pos != std::string::npos && pos + 2 < line.size()) {
                    vendor = line.substr(pos + 2);
                }
            } else if (model.empty() && line.starts_with("model name")) {
                const auto pos = line.find(':');
                if (pos != std::string::npos && pos + 2 < line.size()) {
                    model = line.substr(pos + 2);
                }
            }
            if (!vendor.empty() && !model.empty()) break;
        }
    } catch (...) {}
    return {vendor, model};
}

[[nodiscard]] inline std::vector<int> enumerate_numa_nodes_() noexcept {
    std::vector<int> nodes;
#if __has_include(<filesystem>)
    namespace fs = std::filesystem;
    try {
        if (!fs::exists("/sys/devices/system/node")) return nodes;
        for (auto const& entry : fs::directory_iterator{"/sys/devices/system/node"}) {
            const auto name = entry.path().filename().string();
            if (name.size() < 5 || name.substr(0, 4) != "node") continue;
            const std::string_view tail{name.data() + 4, name.size() - 4};
            int id = 0;
            auto [p, ec] = std::from_chars(tail.data(), tail.data() + tail.size(), id);
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

class Topology : public safety::Pinned<Topology> {
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
        static const Topology inst{};
        return inst;
    }

    [[nodiscard]] static Snapshot reprobe_snapshot() noexcept {
        const Topology fresh{};
        return fresh.snapshot();
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

    [[nodiscard]] std::size_t num_cores() const noexcept { return cores_; }
    [[nodiscard]] std::size_t num_smt_threads() const noexcept { return threads_; }
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

    [[nodiscard]] std::size_t numa_nodes() const noexcept { return cores_on_node_.size(); }

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

    [[nodiscard]] Source source() const noexcept { return source_; }

    // Concurrent callers can interleave, but each line arrives whole.
    void log_summary(FILE* out = stderr) const noexcept {
        std::fprintf(out,
                     "crucible::Topology(source=%s):\n"
                     "  CPU:    vendor=\"%s\" model=\"%s\"\n"
                     "  Cores:  physical=%zu smt_threads=%zu smt_factor=%zu process_allowed=%zu\n"
                     "  Caches: l1d=%zu KB l1i=%zu KB l2=%zu KB l3=%zu MB line=%zu\n"
                     "  Groups: l3_clusters=%zu cores_per_socket=%zu\n"
                     "  NUMA:   nodes=%zu\n"
                     "  Memory: page_size=%zu B hugepage_2mb=%s\n",
                     (source_ == Source::Sysfs ? "Sysfs" : "Fallback"), cpu_vendor_.empty() ? "?" : cpu_vendor_.c_str(),
                     cpu_model_.empty() ? "?" : cpu_model_.c_str(), cores_, threads_, smt_factor(), process_cpus_,
                     l1d_ / 1024, l1i_ / 1024, l2_ / 1024, l3_ / (1024 * 1024), line_, l3_groups_.size(),
                     cores_per_socket(), numa_nodes(), page_size_, (hugepage_2mb_ ? "yes" : "no"));
    }

private:
    // Deliberately small.  Understating the caches makes a workload look
    // larger than the cache and so biases the cost model toward parallelism.
    // Splitting a workload that did not need it costs a constant factor, while
    // failing to split one that did costs a factor of the core count.
    static constexpr std::size_t kFallbackL1d = 32 * 1024;
    static constexpr std::size_t kFallbackL2 = 1024 * 1024;
    static constexpr std::size_t kFallbackL3 = 32ULL * 1024 * 1024;
    static constexpr std::size_t kFallbackLine = 64;

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
    std::vector<std::vector<int>> cores_on_node_;
    std::vector<std::vector<int>> numa_distance_;
    Source source_ = Source::Fallback;

    // Noexcept and total: any failure to read sysfs, however malformed, leaves
    // the defaults in place rather than propagating out.
    Topology() noexcept {
        const unsigned hw = std::thread::hardware_concurrency();
        cores_ = (hw == 0) ? 1 : hw;
        threads_ = cores_;

        const std::size_t allowed = topology_detail::probe_process_cpu_count_();
        process_cpus_ = (allowed > 0) ? allowed : threads_;

        page_size_ = topology_detail::probe_page_size_();

        hugepage_2mb_ = topology_detail::probe_hugepage_2mb_available_();

        auto [vendor, model] = topology_detail::probe_cpu_vendor_and_model_();
        cpu_vendor_ = std::move(vendor);
        cpu_model_ = std::move(model);

        // One node, and the conventional distance from a node to itself.
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

#if __has_include(<filesystem>)
inline void Topology::probe_linux_() noexcept {
    using namespace topology_detail;

    const auto cpus = enumerate_cpus_();
    if (cpus.empty()) return;  // no sysfs: keep the fallback values

    // Sysfs beats the standard library's count when CPUs have been unplugged.
    threads_ = cpus.size();

    // Reading one CPU's caches assumes every core has the same hierarchy.  A
    // part that mixes performance and efficiency cores would need each core
    // probed separately.

    {
        const std::string base = "/sys/devices/system/cpu/cpu" + std::to_string(cpus[0]) + "/cache";
        std::set<int> physical_cores;  // dedup via core_id

        namespace fs = std::filesystem;
        try {
            if (!fs::exists(base)) return;
            for (auto const& entry : fs::directory_iterator{base}) {
                const auto name = entry.path().filename().string();
                if (name.size() < 6 || name.substr(0, 5) != "index") continue;

                const std::string idx_dir = base + "/" + name;
                const auto level_str = read_trimmed_(idx_dir + "/level");
                const auto type_str = read_trimmed_(idx_dir + "/type");
                const auto size_str = read_trimmed_(idx_dir + "/size");
                const auto line_str = read_trimmed_(idx_dir + "/coherency_line_size");

                int level = 0;
                std::from_chars(level_str.data(), level_str.data() + level_str.size(), level);
                const std::size_t size = parse_size_suffix_(size_str);
                std::size_t line = 0;
                std::from_chars(line_str.data(), line_str.data() + line_str.size(), line);

                if (line > 0) line_ = line;
                if (size == 0) continue;

                if (level == 1) {
                    // Sysfs reports the two halves of L1 as separate entries.
                    // Some designs report one unified cache instead, which
                    // stands for both.
                    if (type_str == "Data") {
                        l1d_ = size;
                    } else if (type_str == "Instruction") {
                        l1i_ = size;
                    } else if (type_str == "Unified") {
                        l1d_ = size;
                        l1i_ = size;
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

    // Hardware threads on one core report the same core id, so the number of
    // distinct ids is the number of physical cores.

    {
        std::set<int> physical_core_ids;
        for (int cpu : cpus) {
            const std::string p = "/sys/devices/system/cpu/cpu" + std::to_string(cpu) + "/topology/core_id";
            const auto s = read_trimmed_(p);
            int core_id = -1;
            std::from_chars(s.data(), s.data() + s.size(), core_id);
            if (core_id >= 0) physical_core_ids.insert(core_id);
        }
        if (!physical_core_ids.empty()) {
            cores_ = physical_core_ids.size();
        }
    }

    // Every CPU sharing one L3 reports the same set in shared_cpu_list, so
    // sorting each set and discarding repeats leaves one entry per L3.

    {
        std::vector<std::vector<int>> all_groups;
        std::set<std::vector<int>> seen;
        namespace fs = std::filesystem;
        for (int cpu : cpus) {
            const std::string base = "/sys/devices/system/cpu/cpu" + std::to_string(cpu) + "/cache";
            try {
                if (!fs::exists(base)) continue;
                for (auto const& entry : fs::directory_iterator{base}) {
                    const auto name = entry.path().filename().string();
                    if (name.size() < 6 || name.substr(0, 5) != "index") continue;

                    const std::string idx_dir = base + "/" + name;
                    const auto level_str = read_trimmed_(idx_dir + "/level");
                    int level = 0;
                    std::from_chars(level_str.data(), level_str.data() + level_str.size(), level);
                    if (level != 3) continue;

                    auto group = parse_cpu_list_(read_trimmed_(idx_dir + "/shared_cpu_list"));
                    std::sort(group.begin(), group.end());
                    if (group.empty()) continue;
                    if (seen.insert(group).second) {
                        all_groups.push_back(std::move(group));
                    }
                    break;  // one L3 per CPU
                }
            } catch (...) {
                // skip this cpu
            }
        }
        if (!all_groups.empty()) {
            l3_groups_ = std::move(all_groups);
        }
    }

    {
        const auto nodes = enumerate_numa_nodes_();
        if (!nodes.empty()) {
            std::vector<std::vector<int>> new_cores_on_node;
            std::vector<std::vector<int>> new_numa_distance;

            for (int node : nodes) {
                const std::string base = "/sys/devices/system/node/node" + std::to_string(node);
                auto cpus_on_node = parse_cpu_list_(read_trimmed_(base + "/cpulist"));
                std::sort(cpus_on_node.begin(), cpus_on_node.end());
                new_cores_on_node.push_back(std::move(cpus_on_node));

                auto dist_row = parse_int_list_(read_trimmed_(base + "/distance"));
                if (dist_row.empty()) {
                    // The conventional distances: 10 to itself, 20 elsewhere.
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

static_assert(std::is_class_v<Topology>);
// The address of the singleton is the identity of the probed data, so it
// neither copies nor moves.
static_assert(!std::is_copy_constructible_v<Topology>);
static_assert(!std::is_move_constructible_v<Topology>);

}  // namespace crucible::concurrent
