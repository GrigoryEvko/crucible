#pragma once

// Can this host measure anything at all?
//
// A number is not evidence. A number taken on a core that shares a pipeline
// with a busy sibling, at a clock the governor is free to move, on a box
// carrying someone else's load, is a number about the weather. The ledger
// records the weather alongside the number so a reader can tell the two
// apart.
//
// This box has produced meaningless numbers three separate ways: a bench
// block ran on cores whose SMT siblings were online, sat at its 1.22 GHz
// floor, and had no NUMA binding, all at once, and reported to four
// significant figures throughout. Nothing in the numbers said so. That is
// the failure this header exists to make impossible: every probe carries a
// competence report, and a report with any defect caps the verdict at low
// confidence no matter how tidy the samples look.
//
// The report never blocks a measurement. A degraded host still measures and
// still writes entries — it writes them at low confidence with the defect
// named, and the default reader refuses to serve them. Refusing to measure
// would leave the runtime with nothing; refusing to *trust* leaves it with
// the conservative path, which is always available.

#include <crucible/ledger/HostFingerprint.h>
#include <fixy/Bits.h>
#include <fixy/concurrent/Topology.h>
#include <foundation/effects/Ctx.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace crucible::ledger {

// Each bit names one reason the host cannot be trusted to measure. The
// enumerators are frozen by bit position: an entry on disk carries the raw
// word, so renumbering silently reinterprets every stored report.
enum class CompetenceDefect : std::uint16_t {
    NoIsolatedCores = 1u << 0,  // nothing is fenced off from the scheduler
    SmtSiblingOnline = 1u << 1,  // an isolated core shares its pipeline
    ClockNotPinned = 1u << 2,  // the governor may move the frequency
    LoadAverageHigh = 1u << 3,  // someone else is using the machine
    PerfEventRestricted = 1u << 4,  // no access to the counter subsystem
    TopologyNotProbed = 1u << 5,  // cache geometry is a guess, not a read
};

[[nodiscard]] constexpr std::string_view competence_defect_name(CompetenceDefect defect) noexcept {
    switch (defect) {
        case CompetenceDefect::NoIsolatedCores:
            return "NoIsolatedCores";
        case CompetenceDefect::SmtSiblingOnline:
            return "SmtSiblingOnline";
        case CompetenceDefect::ClockNotPinned:
            return "ClockNotPinned";
        case CompetenceDefect::LoadAverageHigh:
            return "LoadAverageHigh";
        case CompetenceDefect::PerfEventRestricted:
            return "PerfEventRestricted";
        case CompetenceDefect::TopologyNotProbed:
            return "TopologyNotProbed";
        default:
            return "<unknown CompetenceDefect>";
    }
}

// ── Thresholds ────────────────────────────────────────────────────────
//
// Each is a stated bar rather than a feeling, so a host that fails one can
// be pointed at the number it failed.

// A floor within this much of the ceiling counts as pinned. The bench host
// runs powersave with min == max, which is pinned in fact even though the
// governor name says otherwise, so the test is on the frequencies and not
// on the string.
inline constexpr std::uint32_t kClockPinnedToleranceBasisPoints = 500;  // 5%

// Load is only meaningful against capacity: 100 is idle on a 384-thread
// box and catastrophic on a 4-thread one. The bar is half the machine's
// hardware threads.
//
// Against the MACHINE's threads, not the process's allowed set. The load
// average in /proc/loadavg counts every runnable task on the host, so the
// only denominator that makes it a ratio is the host's own capacity. Using
// the allowed set instead — which is what this first did — reports a
// pinned process on a busy box as degraded even when it is pinned to
// isolated cores the busy work cannot reach. Running the probe under
// `taskset -c 88-95` on this host produced exactly that: load 156 against
// 8 allowed CPUs read as 1957% and flagged, while the measurement itself
// was on isolcpus and entirely uncontended.
//
// Against isolated cores this is still pessimistic, since general work
// cannot be scheduled there at all. Pessimistic is the safe direction: it
// caps confidence at low rather than promoting a bad number.
inline constexpr std::uint32_t kLoadBudgetPercentOfCpus = 50;

// 2 permits per-process measurement, which is all a self-profiling probe
// needs. 3 and above locks the subsystem entirely.
inline constexpr std::int32_t kMaxPerfEventParanoid = 2;

// Each pad member holds bytes that the compiler would otherwise pad, so the
// report has no padding byte.  With -ftrivial-auto-var-init=zero, GCC writes
// zero to each padding hole of a Ledger at each return of one, one store for
// each hole.  test/test_ledger.cpp makes sure that the report has no padding
// byte.
struct CompetenceReport {
    ::fixy::Bits<CompetenceDefect> defects{};
    std::uint8_t pad_after_defects[2]{};

    // The facts behind the defects, kept so an operator reading the ledger
    // can see how far off the host was rather than only that it was off.
    std::uint32_t isolated_core_count = 0;
    std::uint32_t online_sibling_count = 0;
    std::uint32_t load_average_milli = 0;  // one-minute load average x 1000

    // Both counts are kept. The allowed set is what a thread pool sizes
    // against; the machine count is what the load average is a ratio of.
    // Conflating them is what made a pinned process on isolated cores read
    // as overloaded.
    std::uint32_t allowed_cpu_count = 0;
    std::uint32_t machine_cpu_count = 0;
    std::int32_t perf_event_paranoid = 0;
    std::uint8_t pad_after_paranoid[4]{};
    std::uint64_t scaling_min_freq_khz = 0;
    std::uint64_t scaling_max_freq_khz = 0;
    bool governor_is_performance = false;
    std::uint8_t pad_tail[7]{};

    // The single question every confidence decision asks.
    [[nodiscard]] constexpr bool is_competent() const noexcept { return defects.raw() == 0u; }

    [[nodiscard]] constexpr std::uint16_t defect_word() const noexcept { return defects.raw(); }
};

// Writes the defect names into `into` as a comma-separated list and
// returns the view. "none" when the report is clean. The buffer is the
// caller's so the function allocates nothing.
[[nodiscard]] inline std::string_view describe_defects(CompetenceReport const& report, std::span<char> into) noexcept {
    if (into.empty()) {
        return {};
    }
    into[0] = '\0';
    if (report.is_competent()) {
        constexpr std::string_view kNone = "none";
        if (into.size() > kNone.size()) {
            std::memcpy(into.data(), kNone.data(), kNone.size());
            into[kNone.size()] = '\0';
            return std::string_view{into.data(), kNone.size()};
        }
        return {};
    }
    std::size_t written = 0;
    constexpr CompetenceDefect kAll[] = {
        CompetenceDefect::NoIsolatedCores, CompetenceDefect::SmtSiblingOnline,    CompetenceDefect::ClockNotPinned,
        CompetenceDefect::LoadAverageHigh, CompetenceDefect::PerfEventRestricted, CompetenceDefect::TopologyNotProbed,
    };
    for (const CompetenceDefect defect : kAll) {
        if ((report.defects.raw() & static_cast<std::uint16_t>(defect)) == 0u) {
            continue;
        }
        const std::string_view name = competence_defect_name(defect);
        const std::size_t separator = (written == 0u) ? 0u : 1u;
        if (written + separator + name.size() + 1u > into.size()) {
            break;
        }
        if (separator != 0u) {
            into[written] = ',';
            ++written;
        }
        std::memcpy(into.data() + written, name.data(), name.size());
        written += name.size();
    }
    into[written] = '\0';
    return std::string_view{into.data(), written};
}

// Each probe below reads procfs or sysfs, so each takes a context that
// admits the read, the gate of HostFingerprint.h.
namespace competence_detail {

// The set of CPUs the kernel currently has online, as a sorted list.
template <::foundation::effects::IsExecCtx Ctx>
    requires CtxFitsHostProbe<Ctx>
[[nodiscard]] inline std::vector<int> read_online_cpus(Ctx const& ctx) noexcept {
    fingerprint_detail::SmallFileBuffer buffer{};
    return ::fixy::concurrent::topology_detail::parse_cpu_list_(
        fingerprint_detail::read_small_file(ctx, "/sys/devices/system/cpu/online", buffer));
}

template <::foundation::effects::IsExecCtx Ctx>
    requires CtxFitsHostProbe<Ctx>
[[nodiscard]] inline std::vector<int> read_isolated_cpus(Ctx const& ctx) noexcept {
    fingerprint_detail::SmallFileBuffer buffer{};
    return ::fixy::concurrent::topology_detail::parse_cpu_list_(
        fingerprint_detail::read_small_file(ctx, "/sys/devices/system/cpu/isolated", buffer));
}

// The one-minute load average, scaled by 1000 so the report stays integral
// and so a stored entry has no float to round differently on read-back.
template <::foundation::effects::IsExecCtx Ctx>
    requires CtxFitsHostProbe<Ctx>
[[nodiscard]] inline std::uint32_t read_load_average_milli(Ctx const& ctx) noexcept {
    fingerprint_detail::SmallFileBuffer buffer{};
    const std::string_view loadavg = fingerprint_detail::read_small_file(ctx, "/proc/loadavg", buffer);
    if (loadavg.empty()) {
        return 0u;
    }
    const std::size_t space = loadavg.find(' ');
    const std::string_view first = loadavg.substr(0, space);
    const std::size_t dot = first.find('.');
    const std::uint64_t whole = fingerprint_detail::parse_unsigned(first.substr(0, dot), 0u);
    std::uint64_t fraction = 0;
    if (dot != std::string_view::npos) {
        std::string_view digits = first.substr(dot + 1u);
        digits = digits.substr(0, std::min<std::size_t>(digits.size(), 3u));
        fraction = fingerprint_detail::parse_unsigned(digits, 0u);
        for (std::size_t pad = digits.size(); pad < 3u; ++pad) {
            fraction *= 10u;
        }
    }
    const std::uint64_t milli = whole * 1000u + fraction;
    return static_cast<std::uint32_t>(std::min<std::uint64_t>(milli, 0xFFFFFFFFull));
}

template <::foundation::effects::IsExecCtx Ctx>
    requires CtxFitsHostProbe<Ctx>
[[nodiscard]] inline std::int32_t read_perf_event_paranoid(Ctx const& ctx) noexcept {
    fingerprint_detail::SmallFileBuffer buffer{};
    const std::string_view text =
        fingerprint_detail::read_small_file(ctx, "/proc/sys/kernel/perf_event_paranoid", buffer);
    if (text.empty()) {
        // Absent means the subsystem is not built in, which is at least as
        // restrictive as the highest setting.
        return 4;
    }
    if (text.front() == '-') {
        const std::uint64_t magnitude = fingerprint_detail::parse_unsigned(text.substr(1), 1u);
        return -static_cast<std::int32_t>(std::min<std::uint64_t>(magnitude, 0x7FFFFFFFull));
    }
    return static_cast<std::int32_t>(
        std::min<std::uint64_t>(fingerprint_detail::parse_unsigned(text, 4u), 0x7FFFFFFFull));
}

// How many hardware threads share a pipeline with an isolated core and are
// still online. Any non-zero count means the isolated core is not actually
// alone: its sibling can be scheduled onto at any moment and will contend
// for the same front end.
template <::foundation::effects::IsExecCtx Ctx>
    requires CtxFitsHostProbe<Ctx>
[[nodiscard]] inline std::uint32_t count_online_siblings_of(Ctx const& ctx, std::span<const int> isolated,
                                                            std::span<const int> online) noexcept {
    std::uint32_t contended = 0;
    char path[160]{};
    for (const int isolated_cpu : isolated) {
        std::snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/topology/thread_siblings_list", isolated_cpu);
        fingerprint_detail::SmallFileBuffer buffer{};
        const std::vector<int> siblings = ::fixy::concurrent::topology_detail::parse_cpu_list_(
            fingerprint_detail::read_small_file(ctx, path, buffer));
        for (const int sibling : siblings) {
            if (sibling == isolated_cpu) {
                continue;
            }
            if (std::find(online.begin(), online.end(), sibling) != online.end()) {
                ++contended;
            }
        }
    }
    return contended;
}

// True when every cpufreq policy names the performance governor. A host
// with no cpufreq at all answers false, which is the conservative reading:
// absence of evidence that the clock is pinned is not evidence that it is.
template <::foundation::effects::IsExecCtx Ctx>
    requires CtxFitsHostProbe<Ctx>
[[nodiscard]] inline bool all_policies_are_performance(Ctx const& ctx) noexcept {
    char path[160]{};
    bool saw_any_policy = false;
    for (unsigned policy_index = 0; policy_index < 256u; ++policy_index) {
        std::snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpufreq/policy%u/scaling_governor", policy_index);
        fingerprint_detail::SmallFileBuffer buffer{};
        const std::string_view governor = fingerprint_detail::read_small_file(ctx, path, buffer);
        if (governor.empty()) {
            break;
        }
        saw_any_policy = true;
        if (governor != "performance") {
            return false;
        }
    }
    return saw_any_policy;
}

}  // namespace competence_detail

// Derives the defect word from facts already in hand. Split out from the
// probe so a test can hand it a synthetic host and get a deterministic
// answer without touching sysfs.
[[nodiscard]] constexpr ::fixy::Bits<CompetenceDefect> derive_defects(CompetenceReport const& facts,
                                                                      bool was_topology_probed) noexcept {
    ::fixy::Bits<CompetenceDefect> defects{};

    if (facts.isolated_core_count == 0u) {
        defects.set(CompetenceDefect::NoIsolatedCores);
    }
    if (facts.online_sibling_count != 0u) {
        defects.set(CompetenceDefect::SmtSiblingOnline);
    }

    // Pinned by name or pinned in fact. The bench host is the second case:
    // powersave with the floor raised to the ceiling holds the clock just
    // as still as the performance governor does.
    const bool floor_meets_ceiling =
        facts.scaling_max_freq_khz != 0u
        && (facts.scaling_max_freq_khz - std::min(facts.scaling_min_freq_khz, facts.scaling_max_freq_khz)) * 10000u
               <= facts.scaling_max_freq_khz * kClockPinnedToleranceBasisPoints;
    if (!facts.governor_is_performance && !floor_meets_ceiling) {
        defects.set(CompetenceDefect::ClockNotPinned);
    }

    // Guard the multiply: a host reporting an absurd CPU count must not
    // overflow the budget into a number every load passes.
    const std::uint64_t load_budget_milli = static_cast<std::uint64_t>(facts.machine_cpu_count)
                                          * static_cast<std::uint64_t>(kLoadBudgetPercentOfCpus) * 10u;
    // A machine that reports no CPUs, or a process permitted to run on
    // none, is a failed probe rather than an idle host.
    if (facts.machine_cpu_count == 0u || facts.allowed_cpu_count == 0u
        || static_cast<std::uint64_t>(facts.load_average_milli) > load_budget_milli) {
        defects.set(CompetenceDefect::LoadAverageHigh);
    }

    if (facts.perf_event_paranoid > kMaxPerfEventParanoid) {
        defects.set(CompetenceDefect::PerfEventRestricted);
    }
    if (!was_topology_probed) {
        defects.set(CompetenceDefect::TopologyNotProbed);
    }
    return defects;
}

template <::foundation::effects::IsExecCtx Ctx>
    requires CtxFitsHostProbe<Ctx>
[[nodiscard]] inline CompetenceReport probe_competence(Ctx const& ctx) noexcept {
    const ::fixy::concurrent::Topology::Snapshot topology = ::fixy::concurrent::Topology::instance().snapshot();
    const std::vector<int> isolated = competence_detail::read_isolated_cpus(ctx);
    const std::vector<int> online = competence_detail::read_online_cpus(ctx);

    CompetenceReport report{};
    report.isolated_core_count = static_cast<std::uint32_t>(isolated.size());
    report.online_sibling_count = competence_detail::count_online_siblings_of(ctx, isolated, online);
    report.load_average_milli = competence_detail::read_load_average_milli(ctx);
    report.allowed_cpu_count = static_cast<std::uint32_t>(topology.process_cpu_count);
    report.machine_cpu_count = static_cast<std::uint32_t>(topology.num_smt_threads);
    report.perf_event_paranoid = competence_detail::read_perf_event_paranoid(ctx);
    report.governor_is_performance = competence_detail::all_policies_are_performance(ctx);

    // Read from the same place the fingerprint's policy half reads, so the
    // two never disagree about what the clock is doing.
    const HostFacts facts = probe_host_facts(ctx);
    report.scaling_min_freq_khz = facts.scaling_min_freq_khz;
    report.scaling_max_freq_khz = facts.scaling_max_freq_khz;

    report.defects = derive_defects(report, topology.source == ::fixy::concurrent::Topology::Source::Sysfs);
    return report;
}

}  // namespace crucible::ledger
