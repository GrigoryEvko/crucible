#include <fixy/concurrent/Topology.h>

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdio>
#include <fstream>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
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

// The probe of the topology.  It runs one time for each process, so this
// file is the one place that compiles it, and an includer of Topology.h
// compiles none of the sysfs reads, the file streams or the directory walks.

namespace fixy::concurrent {

namespace topology_detail {

std::size_t parse_size_suffix_(std::string_view s) noexcept {
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

std::vector<int> parse_cpu_list_(std::string_view s) noexcept {
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

std::vector<int> parse_int_list_(std::string_view s) noexcept {
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

namespace {

// Empty on any failure to read.  The trailing newline every sysfs file ends
// with is trimmed off.
[[nodiscard]] std::string read_trimmed_(std::string_view path) noexcept {
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

// The CPU directory holds entries such as "cpufreq" and "cpuidle" alongside
// the per-CPU ones, so only a "cpu" followed by digits alone counts.
//
// Returns the ONLINE CPUs, not the present ones.  A `cpuN` directory survives
// the CPU going offline — only `cpuN/online` flips to 0 — so the directory
// glob alone counts hardware the process can never run on.  Every other sysfs
// source this file reads (node `cpulist`, cache `shared_cpu_list`) already
// reports online CPUs only, and mixing the two sets makes `num_smt_threads()`
// disagree with the union of `cores_on_node()`.
//
// Measured on the bench host with cores 280-287 offlined for SMT isolation:
// 384 `cpuN` directories against 376 CPUs in the two node cpulists.  A caller
// sizing a per-CPU array or a thread pool from the larger number reserves for
// eight CPUs that cannot be scheduled on, and a scheduler pinning by index can
// pick one of them.
//
// `cpu0` ships no `online` file on most kernels because it cannot be offlined,
// and a kernel built without CPU hotplug ships none at all.  A missing file
// therefore reads as online, which is the fail-open direction that keeps this
// probe working on those kernels rather than returning an empty set.
[[nodiscard]] std::vector<int> enumerate_cpus_(std::string_view sysfs_root) noexcept {
    std::vector<int> cpus;
#if __has_include(<filesystem>)
    namespace fs = std::filesystem;
    try {
        const std::string cpu_dir = std::string{sysfs_root} + "/devices/system/cpu";
        if (!fs::exists(cpu_dir)) return cpus;
        for (auto const& entry : fs::directory_iterator{cpu_dir}) {
            const auto name = entry.path().filename().string();
            if (name.size() < 4 || name.substr(0, 3) != "cpu") continue;
            const std::string_view tail{name.data() + 3, name.size() - 3};
            int id = 0;
            auto [p, ec] = std::from_chars(tail.data(), tail.data() + tail.size(), id);
            if (ec != std::errc{} || p != tail.data() + tail.size() || id < 0) continue;
            const std::string online_path = cpu_dir + "/" + name + "/online";
            if (fs::exists(online_path) && read_trimmed_(online_path) == "0") continue;
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
[[nodiscard]] std::size_t probe_process_cpu_count_() noexcept {
#if CRUCIBLE_HAS_SCHED_AFFINITY
    cpu_set_t mask;
    CPU_ZERO(&mask);
    if (::sched_getaffinity(0, sizeof(mask), &mask) == 0) {
        return static_cast<std::size_t>(CPU_COUNT(&mask));
    }
#endif
    return 0;
}

[[nodiscard]] std::size_t probe_page_size_() noexcept {
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
[[nodiscard]] bool probe_hugepage_2mb_available_(std::string_view sysfs_root) noexcept {
    const auto contents = read_trimmed_(std::string{sysfs_root} + "/kernel/mm/transparent_hugepage/enabled");
    if (contents.empty()) return false;
    return contents.find("[always]") != std::string::npos || contents.find("[madvise]") != std::string::npos;
}

// Only the first entry is read.  This assumes every CPU in the machine is the
// same model.
[[nodiscard]] std::pair<std::string, std::string> probe_cpu_vendor_and_model_() noexcept {
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

// The node ids in ascending order.  They are ids, not positions: a host
// with node0 and node2 gives {0, 2}.
[[nodiscard]] std::vector<int> enumerate_numa_nodes_(std::string_view sysfs_root) noexcept {
    std::vector<int> nodes;
#if __has_include(<filesystem>)
    namespace fs = std::filesystem;
    try {
        const std::string node_dir = std::string{sysfs_root} + "/devices/system/node";
        if (!fs::exists(node_dir)) return nodes;
        for (auto const& entry : fs::directory_iterator{node_dir}) {
            const auto name = entry.path().filename().string();
            if (name.size() < 5 || name.substr(0, 4) != "node") continue;
            const std::string_view tail{name.data() + 4, name.size() - 4};
            int id = 0;
            auto [p, ec] = std::from_chars(tail.data(), tail.data() + tail.size(), id);
            if (ec != std::errc{} || p != tail.data() + tail.size() || id < 0) continue;
            nodes.push_back(id);
        }
        std::sort(nodes.begin(), nodes.end());
    } catch (...) {
        nodes.clear();
    }
#endif
    return nodes;
}

#if __has_include(<filesystem>)

// The cache sizes and the line size that the probe found.  The probe sets
// each field to the value that the topology already holds, so a field that
// sysfs does not give keeps the fallback.
struct CacheGeometry {
    std::size_t l1d = 0;
    std::size_t l1i = 0;
    std::size_t l2 = 0;
    std::size_t l3 = 0;
    std::size_t line = 0;
};

// Each of the four steps of the probe below is a function of its own.
// noinline prevents an inline expansion of the four steps into probe_linux_.
// One function with all four steps has more than 60 KB of machine code in a
// Debug build, and the debug-information passes become slow on it.

// Reads the cache directory of one CPU into geometry.  A CPU with no cache
// directory keeps the fallback sizes.
[[gnu::noinline]] void probe_cache_geometry_(std::string const& base, CacheGeometry& geometry) noexcept {
    namespace fs = std::filesystem;
    try {
        const auto entries = fs::exists(base) ? fs::directory_iterator{base} : fs::directory_iterator{};
        for (auto const& entry : entries) {
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

            if (line > 0) geometry.line = line;
            if (size == 0) continue;

            if (level == 1) {
                // Sysfs reports the two halves of L1 as separate entries.
                // Some designs report one unified cache instead, which
                // stands for both.
                if (type_str == "Data") {
                    geometry.l1d = size;
                } else if (type_str == "Instruction") {
                    geometry.l1i = size;
                } else if (type_str == "Unified") {
                    geometry.l1d = size;
                    geometry.l1i = size;
                }
            } else if (level == 2) {
                geometry.l2 = size;
            } else if (level == 3) {
                geometry.l3 = size;
            }
        }
    } catch (...) {
        // Cache probe failed; keep fallback.
    }
}

// Hardware threads on one core report the same core id, so the number of
// distinct ids is the number of physical cores — WITHIN ONE PACKAGE.
// `core_id` is unique per package, not per machine: every socket numbers
// its cores from 0, so socket 1's core 0 and socket 0's core 0 collide.
// Counting bare core_ids therefore reports the cores of a single socket
// and divides the true count by the number of sockets.
//
// Measured on the bench host (2 x EPYC 9655): 96 distinct core_ids
// against 192 distinct (package, core) pairs.  That undercount fed
// `smt_factor()`, which is `threads_ / cores_`, and turned an SMT2
// machine into an apparent SMT4 one — a number plausible enough to pass
// the sanity test in test_topology.cpp, which is why it went unnoticed.
//
// Zero when no CPU gives a core id.
[[nodiscard, gnu::noinline]] std::size_t count_physical_cores_(std::string const& cpu_dir,
                                                               std::vector<int> const& cpus) noexcept {
    std::set<std::pair<int, int>> physical_cores;  // (package, core)
    for (int cpu : cpus) {
        const std::string topo = cpu_dir + "/cpu" + std::to_string(cpu) + "/topology/";
        const auto core_str = read_trimmed_(topo + "core_id");
        int core_id = -1;
        std::from_chars(core_str.data(), core_str.data() + core_str.size(), core_id);
        if (core_id < 0) continue;

        // A single-socket kernel, or one without package topology, ships
        // no physical_package_id.  Package 0 for all is then correct,
        // and reduces this to the original single-package count.
        const auto pkg_str = read_trimmed_(topo + "physical_package_id");
        int package_id = 0;
        std::from_chars(pkg_str.data(), pkg_str.data() + pkg_str.size(), package_id);

        physical_cores.emplace(package_id, core_id);
    }
    return physical_cores.size();
}

// Every CPU sharing one L3 reports the same set in shared_cpu_list, so
// sorting each set and discarding repeats leaves one entry per L3.
//
// No group when no CPU gives an L3.
[[nodiscard, gnu::noinline]] std::vector<std::vector<int>> probe_l3_groups_(std::string const& cpu_dir,
                                                                            std::vector<int> const& cpus) noexcept {
    std::vector<std::vector<int>> all_groups;
    std::set<std::vector<int>> seen;
    namespace fs = std::filesystem;
    for (int cpu : cpus) {
        const std::string base = cpu_dir + "/cpu" + std::to_string(cpu) + "/cache";
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
    return all_groups;
}

// The three parallel tables of the NUMA nodes: the ids, the CPUs and the
// distance rows.
struct NumaTables {
    std::vector<int> node_ids;
    std::vector<std::vector<int>> cores_on_node;
    std::vector<std::vector<int>> distance;
};

// No node when sysfs names none.
[[nodiscard, gnu::noinline]] NumaTables probe_numa_tables_(std::string_view sysfs_root) noexcept {
    NumaTables tables{};
    auto nodes = enumerate_numa_nodes_(sysfs_root);
    if (nodes.empty()) return tables;

    std::vector<std::vector<int>> new_cores_on_node(nodes.size());
    std::vector<std::vector<int>> new_numa_distance(nodes.size());

    for (std::size_t position = 0; position < nodes.size(); ++position) {
        const std::string base =
            std::string{sysfs_root} + "/devices/system/node/node" + std::to_string(nodes[position]);
        auto cpus_on_node = parse_cpu_list_(read_trimmed_(base + "/cpulist"));
        std::sort(cpus_on_node.begin(), cpus_on_node.end());
        new_cores_on_node[position] = std::move(cpus_on_node);

        // A row with one entry per node indexes by position.  A
        // missing row, or one of another length, reads as the
        // conventional distances: 10 to the node itself and 20 to
        // each other node.
        auto dist_row = parse_int_list_(read_trimmed_(base + "/distance"));
        if (dist_row.size() != nodes.size()) {
            dist_row.assign(nodes.size(), 20);
            dist_row[position] = 10;
        }
        new_numa_distance[position] = std::move(dist_row);
    }

    tables.node_ids = std::move(nodes);
    tables.cores_on_node = std::move(new_cores_on_node);
    tables.distance = std::move(new_numa_distance);
    return tables;
}

#endif  // __has_include(<filesystem>)

}  // namespace

}  // namespace topology_detail

Topology::Topology(std::string_view sysfs_root) noexcept {
    const unsigned hw = std::thread::hardware_concurrency();
    cores_ = (hw == 0) ? 1 : hw;
    threads_ = cores_;

    const std::size_t allowed = topology_detail::probe_process_cpu_count_();
    process_cpus_ = (allowed > 0) ? allowed : threads_;

    page_size_ = topology_detail::probe_page_size_();

    hugepage_2mb_ = topology_detail::probe_hugepage_2mb_available_(sysfs_root);

    auto [vendor, model] = topology_detail::probe_cpu_vendor_and_model_();
    cpu_vendor_ = std::move(vendor);
    cpu_model_ = std::move(model);

    // One node with id 0, and the conventional distance from a node to
    // itself.
    numa_node_ids_.push_back(0);
    cores_on_node_.push_back({});
    numa_distance_.push_back({10});
    for (std::size_t i = 0; i < cores_; ++i) {
        cores_on_node_[0].push_back(static_cast<int>(i));
    }

#if __has_include(<filesystem>)
    probe_linux_(sysfs_root);
#endif
}

void Topology::log_summary(FILE* out) const noexcept {
    std::fprintf(out,
                 "fixy::Topology(source=%s):\n"
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

#if __has_include(<filesystem>)
void Topology::probe_linux_(std::string_view sysfs_root) noexcept {
    using namespace topology_detail;

    const std::string cpu_dir = std::string{sysfs_root} + "/devices/system/cpu";
    const auto cpus = enumerate_cpus_(sysfs_root);
    if (cpus.empty()) return;  // no sysfs: keep the fallback values

    // Sysfs beats the standard library's count when CPUs have been unplugged
    // — but only because `enumerate_cpus_` filters on `cpuN/online`.  The
    // directory glob on its own counts unplugged CPUs, which is the opposite
    // of what this line claimed before that filter existed.
    threads_ = cpus.size();

    // Reading one CPU's caches assumes every core has the same hierarchy.  A
    // part that mixes performance and efficiency cores would need each core
    // probed separately.

    // A CPU with no cache directory keeps the fallback sizes, and the probe
    // goes on to the cores, the L3 groups and the NUMA nodes.
    CacheGeometry geometry{.l1d = l1d_, .l1i = l1i_, .l2 = l2_, .l3 = l3_, .line = line_};
    probe_cache_geometry_(cpu_dir + "/cpu" + std::to_string(cpus[0]) + "/cache", geometry);
    l1d_ = geometry.l1d;
    l1i_ = geometry.l1i;
    l2_ = geometry.l2;
    l3_ = geometry.l3;
    line_ = geometry.line;

    const std::size_t physical_cores = count_physical_cores_(cpu_dir, cpus);
    if (physical_cores != 0) {
        cores_ = physical_cores;
    }

    auto all_groups = probe_l3_groups_(cpu_dir, cpus);
    if (!all_groups.empty()) {
        l3_groups_ = std::move(all_groups);
    }

    NumaTables numa = probe_numa_tables_(sysfs_root);
    if (!numa.node_ids.empty()) {
        numa_node_ids_ = std::move(numa.node_ids);
        cores_on_node_ = std::move(numa.cores_on_node);
        numa_distance_ = std::move(numa.distance);
    }

    source_ = Source::Sysfs;
}
#endif  // __has_include(<filesystem>)

}  // namespace fixy::concurrent
