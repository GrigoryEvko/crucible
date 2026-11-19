// The bodies of the CPU queries of crucible/warden/CpuTopology.h.
//
// Each body reads sysfs or procfs.  A gate of the header checks the context
// of the caller and then calls the body here, so no includer compiles a
// body.

#include <crucible/warden/CpuTopology.h>

#include <foundation/os/File.h>
#include <foundation/os/PathText.h>

#include <cstddef>
#include <cstdint>

namespace crucible::warden::detail {

namespace {

// The capacity of the text of one file of sysfs or procfs.  The status of
// a thread on a host of kMaxCpuCount CPUs fits it.
constexpr std::size_t kSmallFileCapacity = 16384;

using SmallFileText = ::fixy::FixedText<kSmallFileCapacity>;

// The capacity of the path of one file of sysfs.
using SysfsPath = ::foundation::os::PathText<128>;

using AffinityMask = ::foundation::algebra::lattices::AffinityMask;

// The characters of text with no newline or carriage return at its end.
// Complexity: linear in the size of text.
[[nodiscard]] constexpr ::fixy::TextView trim_line_end(::fixy::TextView text) noexcept {
    std::size_t end = 0;
    std::size_t position = 0;
    for (char const character : text) {
        ++position;
        if (character != '\n' && character != '\r') {
            end = position;
        }
    }
    return text_prefix(text, end);
}

// The unsigned number of the text of a file, or zero when the text holds
// no such number.
[[nodiscard]] constexpr std::uint64_t parse_file_number(::fixy::TextView text) noexcept {
    return ::fixy::parse<std::uint64_t>(trim_line_end(text)).value_or(0);
}

// Reads the file at path into text.  True when the read succeeded and the
// file fits the text.  A file that fills the text has lost its end, so it
// reads as unreadable.  On a false answer the text holds no character.
[[nodiscard]] bool read_small_file(::fixy::CText path, SmallFileText& text) noexcept {
    bool const is_read = ::foundation::os::read_file(path, text).is_ok();
    if (!is_read || text.size() == kSmallFileCapacity) {
        text.clear();
        return false;
    }
    return true;
}

// The path "/sys/devices/system/cpu/cpu<cpu>" and the leaf after it, in
// path.  False for a negative id and for a path that does not fit.
[[nodiscard]] bool build_cpu_path(int cpu, ::fixy::TextView leaf, SysfsPath& path) noexcept {
    if (cpu < 0) {
        return false;
    }
    return path.append(::fixy::TextView{"/sys/devices/system/cpu/cpu"}).is_ok()
        && path.append_decimal(static_cast<std::uint64_t>(cpu)).is_ok() && path.append(leaf).is_ok();
}

// Reads the file leaf of the sysfs directory of one CPU into text, as
// read_small_file does.  A negative id reads as unreadable.
[[nodiscard]] bool read_cpu_file(int cpu, ::fixy::TextView leaf, SmallFileText& text) noexcept {
    text.clear();
    SysfsPath path{};
    if (!build_cpu_path(cpu, leaf, path)) {
        return false;
    }
    return read_small_file(path.c_text(), text);
}

// The CPU list of the file at path, or the empty set when the file is
// unreadable or holds no such list.
[[nodiscard]] CpuSet read_cpulist_file(::fixy::CText path) noexcept {
    SmallFileText text{};
    if (!read_small_file(path, text)) {
        return CpuSet{};
    }
    return parse_cpulist(trim_line_end(text.view()));
}

}  // namespace

// The loop counts the ids one at a time.  CpuSet::size() counts with a
// popcount, and a build with no POPCNT instruction calls __popcountdi2 of
// libgcc for it, which the crucible library does not import.
int read_online_cpu_count() noexcept {
    CpuSet const online = read_cpulist_file("/sys/devices/system/cpu/online");
    int count = 0;
    for (int const cpu : online) {
        static_cast<void>(cpu);
        ++count;
    }
    return count > 0 ? count : 1;
}

CpuSet read_allowed_cpus_of_thread() noexcept {
    SmallFileText status{};
    if (!read_small_file("/proc/thread-self/status", status)) {
        return CpuSet{};
    }
    return parse_cpus_allowed_list(status.view());
}

CpuSet cpus_of_affinity(::fixy::Option<AffinityMask> mask) noexcept {
    CpuSet allowed{};
    for (AffinityMask const& each : mask) {
        for (std::uint16_t core = 0; core <= AffinityMask::kMaxCore; ++core) {
            if (each.contains(core)) {
                allowed.insert(core);
            }
        }
    }
    if (!allowed.is_empty()) {
        return allowed;
    }
    std::size_t const online = ::fixy::min(static_cast<std::size_t>(read_online_cpu_count()), CpuSet::capacity);
    allowed.insert_range(0, online - 1);
    return allowed;
}

CpuSet read_isolated_cpus() noexcept { return read_cpulist_file("/sys/devices/system/cpu/isolated"); }

CpuSet read_smt_siblings(int cpu) noexcept {
    SmallFileText text{};
    if (!read_cpu_file(cpu, "/topology/thread_siblings_list", text)) {
        return CpuSet{};
    }
    return parse_cpulist(trim_line_end(text.view()));
}

bool read_is_p_core(int cpu) noexcept {
    SmallFileText text{};
    if (!read_cpu_file(cpu, "/topology/core_type", text) || text.size() == 0) {
        return true;
    }
    return ::fixy::find(text.view(), ::fixy::TextView{"Core"}).is_some();
}

int read_numa_node_of_device(::fixy::CText sysfs_numa_node_path) noexcept {
    SmallFileText text{};
    if (!read_small_file(sysfs_numa_node_path, text)) {
        return -1;
    }
    return ::fixy::parse<int>(trim_line_end(text.view()))
        .map([](int node) noexcept { return node < 0 ? -1 : node; })
        .value_or(-1);
}

std::uint64_t read_cpu_cur_freq_khz(int cpu) noexcept {
    SmallFileText text{};
    if (!read_cpu_file(cpu, "/cpufreq/scaling_cur_freq", text)) {
        return 0;
    }
    return parse_file_number(text.view());
}

std::uint64_t read_cpu_max_freq_khz(int cpu) noexcept {
    SmallFileText text{};
    if (!read_cpu_file(cpu, "/cpufreq/cpuinfo_max_freq", text)) {
        return 0;
    }
    return parse_file_number(text.view());
}

int select_hot_cpu_of(::fixy::Side<side::AllowedCpus, CpuSet> const& allowed, TopologyRef topology,
                      CoreSelector const& sel, ::fixy::Side<side::ExcludedCpus, CpuSet> const& exclude) noexcept {
    // The bonuses are multiplied by this before the CPU index is
    // subtracted, so the index can only break ties between equal
    // bonuses and never outweigh one. The scale is the capacity of a
    // CpuSet, so each CPU index is less than it.
    constexpr int kScoreScale = static_cast<int>(CpuSet::capacity);

    if (allowed.value().is_empty()) return -1;

    if (sel.explicit_cpu >= 0 && allowed.value().contains(sel.explicit_cpu)) {
        return sel.explicit_cpu;
    }

    auto try_pool = [&](CpuSet const& pool) noexcept -> int {
        // The starting score is the lowest representable one so that a
        // candidate scoring below zero, which the first CPU does under
        // its penalty, is still chosen when it is the only one here.
        int best = -1;
        int best_score = ::fixy::min_of<int>;
        for (const int c : pool) {
            if (exclude.value().contains(c)) continue;
            if (sel.avoid_smt_sibling) {
                const CpuSet sibs = read_smt_siblings(c);
                bool clash = false;
                for (const int s : sibs)
                    if (s != c && exclude.value().contains(s)) {
                        clash = true;
                        break;
                    }
                if (clash) continue;
            }
            int score = 0;
            if (sel.prefer_p_core && read_is_p_core(c)) score += 4;
            // A CPU that no node of the topology holds is on no node.  The
            // lookup is O(CPUs of the host).
            bool const is_on_hinted_node =
                sel.numa_hint >= 0 && (*topology).numa_node_of_cpu(c).is_some_and([&sel](int node) noexcept {
                    return node == sel.numa_hint;
                });
            if (is_on_hinted_node) score += 2;
            // The penalties are sized against the performance-core
            // bonus above. The larger one outweighs it, so any other
            // CPU beats the first one. The smaller one cancels it, so
            // the sibling loses to an unrelated performance core but
            // still beats an unrelated efficiency core.
            if (sel.avoid_cpu0) {
                if (c == 0) {
                    score -= 8;
                } else if (read_smt_siblings(c).contains(0)) {
                    score -= 4;
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

    // The isolated CPUs that the task may run on come first.
    if (sel.prefer_isolcpu) {
        const CpuSet preferred = allowed.value().intersection(read_isolated_cpus());
        if (!preferred.is_empty()) {
            const int pick = try_pool(preferred);
            if (pick >= 0) return pick;
        }
    }
    const int pick = try_pool(allowed.value());
    if (pick >= 0) return pick;
    for (const int c : allowed.value())
        if (!exclude.value().contains(c)) return c;
    return -1;
}

CpuSet select_warm_cpus_of(CpuSet const& allowed, TopologyRef topology, ::fixy::Option<CpuId> hot_cpu,
                           ::fixy::ElementCount count) noexcept {
    // The node of the hot CPU, or no value with no hot CPU or for a CPU that
    // the topology holds on no node.  The topology names a CPU by an int, and
    // an id past the largest int is on no node.
    ::fixy::Option<int> const hot_node = hot_cpu.match(
        [topology](CpuId const cpu) noexcept -> ::fixy::Option<int> {
            if (cpu.raw() > static_cast<std::uint32_t>(::fixy::max_of<int>)) return ::fixy::none;
            return (*topology).numa_node_of_cpu(static_cast<int>(cpu.raw()));
        },
        [] noexcept -> ::fixy::Option<int> { return ::fixy::none; });

    CpuSet same_numa{};
    CpuSet other{};
    for (const int c : allowed) {
        // The cursor of a CpuSet gives no negative id.
        bool const is_hot_cpu =
            hot_cpu.is_some_and([c](CpuId const& cpu) noexcept { return cpu.raw() == static_cast<std::uint32_t>(c); });
        if (is_hot_cpu) continue;
        bool const is_on_hot_node = hot_node.is_some_and([topology, c](int const& hot) noexcept {
            return (*topology).numa_node_of_cpu(c).is_some_and([hot](int node) noexcept { return node == hot; });
        });
        if (is_on_hot_node)
            same_numa.insert(static_cast<std::size_t>(c));
        else
            other.insert(static_cast<std::size_t>(c));
    }
    CpuSet chosen{};
    std::size_t const wanted = count.raw();
    std::size_t chosen_count = 0;
    auto choose_from = [&chosen, &chosen_count, wanted](CpuSet const& pool) noexcept {
        for (const int c : pool) {
            if (chosen_count >= wanted) return;
            chosen.insert(static_cast<std::size_t>(c));
            ++chosen_count;
        }
    };
    choose_from(same_numa);
    choose_from(other);
    return chosen;
}

}  // namespace crucible::warden::detail
