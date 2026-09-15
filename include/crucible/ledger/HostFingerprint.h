#pragma once

// The cache key of the hardware-capability ledger.
//
// A measured verdict — "AVX-512 beats AVX2 here", "the knee is at 900 KB",
// "a NUMA hop costs 84 ns" — is only true of the host that produced it. The
// fingerprint is the answer to one question: could this verdict have come out
// differently? Every fact that can flip a verdict is folded in. Every fact
// that cannot is left out, because folding it splits the cache on noise.
//
// The fold has two halves, and they are kept apart on purpose.
//
//   hardware — the silicon and its fixed geometry. A change here means a
//              different machine, or the same machine with a different part
//              in it. Nothing measured on the old fingerprint is salvageable.
//
//   policy   — the tunables an operator sets and can set back. A change here
//              means the same silicon under different rules. The old entries
//              stay on disk under their own key and become correct again the
//              moment the operator restores the setting.
//
// The store puts both halves in the filename, so a governor flip lands in a
// new file rather than overwriting a good one.
//
// The stable half reuses mimic::detail::caps_class_projection, which already
// folds cog::TargetCaps into a binary-compatibility class. Reaching into that
// detail namespace is deliberate: a second, independent fold of the same
// fields is exactly the drift surface the ledger is supposed to close. If two
// Cogs are binary-compatible by CogMimic's reckoning, their ISA-shaped
// verdicts have to live under the same key, and the only way to guarantee
// that is to call the same function.
//
// DetSafe (axiom 8): nothing in this header may reach the hashing path. A
// fingerprint names the machine, never the computation. scripts/
// check-detsafe-ledger.sh asserts the include closure stays disjoint.

#include <crucible/cog/TargetCaps.h>
#include <crucible/concurrent/Topology.h>
#include <crucible/handles/FileHandle.h>
#include <crucible/mimic/CogMimic.h>
#include <crucible/safety/Bits.h>
#include <crucible/safety/Tagged.h>

#if defined(__aarch64__)
#include <sys/auxv.h>
#endif

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::ledger {

// ── Strong types over the two halves ──────────────────────────────────
//
// Both halves are 64-bit digests. Without distinct types a caller can
// hand the policy digest where the hardware digest belongs and the
// compiler stays quiet; the store would then key entries on the wrong
// half and serve a verdict measured under a different governor.

struct HardwareDigest {
    std::uint64_t value = 0;

    [[nodiscard]] constexpr std::uint64_t raw() const noexcept { return value; }
    [[nodiscard]] constexpr bool is_unset() const noexcept { return value == 0u; }
    [[nodiscard]] friend constexpr bool operator==(HardwareDigest, HardwareDigest) noexcept = default;
};

struct PolicyDigest {
    std::uint64_t value = 0;

    [[nodiscard]] constexpr std::uint64_t raw() const noexcept { return value; }
    [[nodiscard]] constexpr bool is_unset() const noexcept { return value == 0u; }
    [[nodiscard]] friend constexpr bool operator==(PolicyDigest, PolicyDigest) noexcept = default;
};

struct HostFingerprint {
    HardwareDigest hardware{};
    PolicyDigest policy{};

    [[nodiscard]] friend constexpr bool operator==(HostFingerprint, HostFingerprint) noexcept = default;

    // A fingerprint with either half unset never matches a stored one, so a
    // probe that failed cannot be mistaken for a probe that succeeded.
    [[nodiscard]] constexpr bool is_complete() const noexcept { return !hardware.is_unset() && !policy.is_unset(); }
};

static_assert(sizeof(HardwareDigest) == sizeof(std::uint64_t));
static_assert(sizeof(PolicyDigest) == sizeof(std::uint64_t));
static_assert(std::is_trivially_copyable_v<HostFingerprint>);

// ── Small text reads ──────────────────────────────────────────────────
//
// Everything the probe needs sits in procfs or sysfs as a short text
// file. The read goes through safety::open_read / read_full rather than
// a file stream: the descriptor surface is already covered by the
// syscall-capability allowlist, it never throws, and the bound is
// explicit. A failure yields an empty view, which folds as absence.

namespace fingerprint_detail {

inline constexpr std::size_t kSmallFileBytes = 4096;

using SmallFileBuffer = std::array<char, kSmallFileBytes>;

// Reads at most kSmallFileBytes and trims trailing whitespace. The view
// borrows `into`, so `into` must outlive every use of the result.
[[nodiscard]] inline std::string_view read_small_file(const char* path, SmallFileBuffer& into) noexcept {
    auto opened = safety::open_read(path);
    if (!opened.has_value()) {
        return {};
    }
    // as_writable_bytes rather than a pointer cast: reinterpret_cast is
    // banned tree-wide, and the span form carries the bound along with the
    // retyping instead of re-deriving it.
    auto filled = safety::read_full(*opened, std::as_writable_bytes(std::span<char>{into.data(), into.size() - 1u}));
    if (!filled.has_value()) {
        return {};
    }
    std::size_t length = *filled;
    into[length] = '\0';
    while (length > 0u) {
        const char last = into[length - 1u];
        if (last != '\n' && last != '\r' && last != ' ' && last != '\t') {
            break;
        }
        --length;
    }
    return std::string_view{into.data(), length};
}

// FNV-1a over bytes. Used only to collapse a variable-length text field
// (a model name, a governor string, a cpu list) into one 64-bit
// contribution before it enters the mixer.
[[nodiscard]] constexpr std::uint64_t fold_bytes(std::string_view text) noexcept {
    std::uint64_t accumulator = 0xcbf29ce484222325ULL;
    for (const char byte : text) {
        accumulator ^= static_cast<std::uint64_t>(static_cast<unsigned char>(byte));
        accumulator *= 0x00000100000001B3ULL;
    }
    return accumulator;
}

// The same finalizer CogMimic's caps fold uses. Sharing it keeps the two
// halves of the stable digest mixing consistently with the projection
// they are folded alongside.
[[nodiscard]] constexpr std::uint64_t mix(std::uint64_t accumulator, std::uint64_t contribution) noexcept {
    return mimic::detail::cog_mimic_fmix64(accumulator ^ contribution);
}

// Pulls one `key<sep>value` field out of a colon-delimited procfs block.
// Returns an empty view when the key is absent.
[[nodiscard]] inline std::string_view procfs_field(std::string_view block, std::string_view key) noexcept {
    std::size_t cursor = 0;
    while (cursor < block.size()) {
        const std::size_t line_end = std::min(block.find('\n', cursor), block.size());
        const std::string_view line = block.substr(cursor, line_end - cursor);
        const std::size_t colon = line.find(':');
        if (colon != std::string_view::npos) {
            std::string_view name = line.substr(0, colon);
            while (!name.empty() && (name.back() == ' ' || name.back() == '\t')) {
                name.remove_suffix(1);
            }
            if (name == key) {
                std::string_view value = line.substr(colon + 1u);
                while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
                    value.remove_prefix(1);
                }
                return value;
            }
        }
        cursor = line_end + 1u;
    }
    return {};
}

[[nodiscard]] inline std::uint64_t parse_unsigned(std::string_view text, std::uint64_t fallback) noexcept {
    if (text.empty()) {
        return fallback;
    }
    int base = 10;
    if (text.size() > 2u && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        base = 16;
        text.remove_prefix(2);
    }
    std::uint64_t parsed = 0;
    const auto* first = text.data();
    const auto* last = text.data() + text.size();
    const auto outcome = std::from_chars(first, last, parsed, base);
    if (outcome.ec != std::errc{}) {
        return fallback;
    }
    return parsed;
}

inline void copy_into(std::span<char> destination, std::string_view source) noexcept {
    if (destination.empty()) {
        return;
    }
    const std::size_t copied = std::min(source.size(), destination.size() - 1u);
    std::memcpy(destination.data(), source.data(), copied);
    destination[copied] = '\0';
}

}  // namespace fingerprint_detail

// ── The probed facts ──────────────────────────────────────────────────
//
// Trivially copyable and fixed-size, so a caller can hold it by value and
// the store can print it without owning a heap. Variable-length material
// (the NUMA distance matrix, the isolated-CPU list, the governor strings)
// is folded into a digest field at probe time rather than carried whole.

inline constexpr std::size_t kVendorBytes = 32;
inline constexpr std::size_t kModelBytes = 96;

struct HostFacts {
    // ── Stable: silicon identity ──
    std::array<char, kVendorBytes> cpu_vendor{};
    std::array<char, kModelBytes> cpu_model{};
    std::uint32_t cpu_family = 0;
    std::uint32_t cpu_model_number = 0;
    std::uint32_t cpu_stepping = 0;
    std::uint64_t microcode_revision = 0;

    // ── Stable: fixed geometry ──
    std::uint32_t l1d_bytes = 0;
    std::uint32_t l1i_bytes = 0;
    std::uint32_t l2_bytes = 0;
    std::uint64_t l3_total_bytes = 0;
    std::uint32_t cache_line_bytes = 0;
    std::uint16_t physical_core_count = 0;
    std::uint16_t hw_thread_count = 0;
    std::uint16_t l3_instance_count = 0;  // one per CCD on a chiplet part
    std::uint8_t numa_node_count = 0;
    std::uint64_t numa_distance_digest = 0;
    std::uint32_t page_size_bytes = 0;

    // Implementation-defined between 128 and 2048 bits and different on
    // Graviton 3, Graviton 4 and A64FX, so two arm64 hosts that agree on
    // every other field still disagree on what a vectorized verdict means.
    // Zero on a host with no scalable vector unit.
    std::uint16_t sve_vector_length_bits = 0;

    safety::Bits<cog::CpuFeature> isa_features{};

    // ── Volatile: operator policy ──
    std::uint64_t governor_digest = 0;  // folded over every cpufreq policy
    std::uint64_t epp_digest = 0;  // energy_performance_preference
    std::uint64_t scaling_min_freq_khz = 0;
    std::uint64_t scaling_max_freq_khz = 0;
    std::uint64_t thp_digest = 0;  // enabled + defrag
    std::uint64_t isolated_cpu_digest = 0;  // isolcpus
    std::uint64_t nohz_full_digest = 0;
    std::uint8_t smt_control = 0;  // 0 unknown, 1 off, 2 on, 3 forceoff
    bool was_probed_from_sysfs = false;

    [[nodiscard]] std::string_view vendor_view() const noexcept { return std::string_view{cpu_vendor.data()}; }
    [[nodiscard]] std::string_view model_view() const noexcept { return std::string_view{cpu_model.data()}; }
};

static_assert(std::is_trivially_copyable_v<HostFacts>);

// ── ISA feature bits ──────────────────────────────────────────────────
//
// Folded into the stable half because a verdict about which vector width
// wins is meaningless on a host that lacks the width. __builtin_cpu_supports
// resolves against the running CPU rather than the -march= the binary was
// compiled for, which is the distinction that matters: a binary built for
// x86-64-v3 running on a v4 part must still learn that AVX-512 exists.

[[nodiscard]] inline safety::Bits<cog::CpuFeature> probe_isa_features() noexcept {
    safety::Bits<cog::CpuFeature> features{};
#if defined(__x86_64__) || defined(__i386__)
    if (__builtin_cpu_supports("avx2")) features.set(cog::CpuFeature::Avx2);
    if (__builtin_cpu_supports("avx512f")) features.set(cog::CpuFeature::Avx512);
    if (__builtin_cpu_supports("avx512vnni") || __builtin_cpu_supports("avxvnni")) {
        features.set(cog::CpuFeature::Vnni);
    }
    if (__builtin_cpu_supports("avx512bf16") || __builtin_cpu_supports("avxneconvert")) {
        features.set(cog::CpuFeature::Bf16Cpu);
    }
    if (__builtin_cpu_supports("avx512fp16")) features.set(cog::CpuFeature::Fp16Cpu);
    if (__builtin_cpu_supports("aes")) features.set(cog::CpuFeature::Aes);
    if (__builtin_cpu_supports("sha")) features.set(cog::CpuFeature::Sha);
#if defined(__x86_64__)
    if (__builtin_cpu_supports("amx-tile")) features.set(cog::CpuFeature::Amx);
#endif
#elif defined(__aarch64__)
    // Every armv8-a part has Advanced SIMD, so it is asserted rather than
    // probed. The scalable extensions are read from the hwcap the kernel
    // published at exec, which needs no syscall of our own.
    features.set(cog::CpuFeature::Neon);
    const unsigned long hwcap = ::getauxval(AT_HWCAP);
    const unsigned long hwcap2 = ::getauxval(AT_HWCAP2);
#if defined(HWCAP_SVE)
    if ((hwcap & HWCAP_SVE) != 0ul) features.set(cog::CpuFeature::Sve);
#endif
#if defined(HWCAP2_SVE2)
    if ((hwcap2 & HWCAP2_SVE2) != 0ul) features.set(cog::CpuFeature::Sve2);
#endif
#if defined(HWCAP2_SME)
    if ((hwcap2 & HWCAP2_SME) != 0ul) features.set(cog::CpuFeature::Sme);
#endif
#if defined(HWCAP2_MTE)
    if ((hwcap2 & HWCAP2_MTE) != 0ul) features.set(cog::CpuFeature::Mte);
#endif
#if defined(HWCAP_PACA)
    if ((hwcap & HWCAP_PACA) != 0ul) features.set(cog::CpuFeature::PauthArm);
#endif
#if defined(HWCAP_AES)
    if ((hwcap & HWCAP_AES) != 0ul) features.set(cog::CpuFeature::Aes);
#endif
#if defined(HWCAP_SHA2)
    if ((hwcap & HWCAP_SHA2) != 0ul) features.set(cog::CpuFeature::Sha);
#endif
    (void)hwcap;
    (void)hwcap2;
#endif
    return features;
}

// Bits, not bytes: the vector-length register is specified in bits and
// reporting it in the unit the architecture uses removes one conversion
// from every comparison. Zero when the host has no scalable vector unit.
[[nodiscard]] inline std::uint16_t probe_sve_vector_length_bits() noexcept {
#if defined(__aarch64__)
    fingerprint_detail::SmallFileBuffer buffer{};
    // The kernel reports the default length in BYTES.
    const std::string_view text =
        fingerprint_detail::read_small_file("/proc/sys/abi/sve_default_vector_length", buffer);
    const std::uint64_t bytes = fingerprint_detail::parse_unsigned(text, 0u);
    if (bytes == 0u || bytes > 256u) {
        return 0u;
    }
    return static_cast<std::uint16_t>(bytes * 8u);
#else
    return 0u;
#endif
}

// ── The probe ─────────────────────────────────────────────────────────

[[nodiscard]] inline HostFacts probe_host_facts() noexcept {
    namespace probe = fingerprint_detail;

    HostFacts facts{};
    const concurrent::Topology::Snapshot topology = concurrent::Topology::instance().snapshot();

    probe::copy_into(facts.cpu_vendor, concurrent::Topology::instance().cpu_vendor());
    probe::copy_into(facts.cpu_model, concurrent::Topology::instance().cpu_model_name());

    facts.l1d_bytes = static_cast<std::uint32_t>(topology.l1d_per_core_bytes);
    facts.l1i_bytes = static_cast<std::uint32_t>(topology.l1i_per_core_bytes);
    facts.l2_bytes = static_cast<std::uint32_t>(topology.l2_per_core_bytes);
    facts.l3_total_bytes = topology.l3_total_bytes;
    facts.cache_line_bytes = static_cast<std::uint32_t>(topology.cache_line_bytes);
    facts.physical_core_count = static_cast<std::uint16_t>(topology.num_cores);
    facts.hw_thread_count = static_cast<std::uint16_t>(topology.num_smt_threads);
    facts.numa_node_count = static_cast<std::uint8_t>(topology.numa_nodes);
    facts.page_size_bytes = static_cast<std::uint32_t>(topology.page_size_bytes);
    facts.l3_instance_count = static_cast<std::uint16_t>(concurrent::Topology::instance().l3_groups().size());
    facts.was_probed_from_sysfs = (topology.source == concurrent::Topology::Source::Sysfs);

    // The full matrix, not just the node count: two hosts with four nodes
    // each can have different hop costs, and a verdict about a remote
    // access is only portable between hosts whose distances agree.
    {
        std::uint64_t accumulator = 0x9e3779b97f4a7c15ULL;
        const int node_count = static_cast<int>(topology.numa_nodes);
        for (int from_node = 0; from_node < node_count; ++from_node) {
            for (int to_node = 0; to_node < node_count; ++to_node) {
                const std::uint64_t distance =
                    static_cast<std::uint64_t>(concurrent::Topology::instance().numa_distance(from_node, to_node));
                accumulator = probe::mix(accumulator, distance);
            }
        }
        facts.numa_distance_digest = accumulator;
    }

    facts.isa_features = probe_isa_features();
    facts.sve_vector_length_bits = probe_sve_vector_length_bits();

    // Stepping and microcode come from procfs. A microcode roll can change
    // which instruction is fast without changing anything else the probe
    // can see, so leaving it out would let a stale verdict survive the one
    // update most likely to invalidate it.
    {
        probe::SmallFileBuffer buffer{};
        const std::string_view cpuinfo = probe::read_small_file("/proc/cpuinfo", buffer);
        facts.cpu_family =
            static_cast<std::uint32_t>(probe::parse_unsigned(probe::procfs_field(cpuinfo, "cpu family"), 0u));
        facts.cpu_model_number =
            static_cast<std::uint32_t>(probe::parse_unsigned(probe::procfs_field(cpuinfo, "model"), 0u));
        facts.cpu_stepping =
            static_cast<std::uint32_t>(probe::parse_unsigned(probe::procfs_field(cpuinfo, "stepping"), 0u));
        facts.microcode_revision = probe::parse_unsigned(probe::procfs_field(cpuinfo, "microcode"), 0u);
        if (facts.cpu_stepping == 0u) {
            facts.cpu_stepping =
                static_cast<std::uint32_t>(probe::parse_unsigned(probe::procfs_field(cpuinfo, "CPU revision"), 0u));
        }
    }

    // ── Volatile half ──
    {
        probe::SmallFileBuffer buffer{};
        std::uint64_t governor_accumulator = 0x517cc1b727220a95ULL;
        std::uint64_t epp_accumulator = 0x2545f4914f6cdd1dULL;
        std::uint64_t min_freq = 0;
        std::uint64_t max_freq = 0;

        // Every policy, not just policy0: a heterogeneous part can run its
        // performance and efficiency clusters under different governors,
        // and a verdict measured with one cluster pinned is not the verdict
        // that holds when both are.
        char path[160]{};
        for (unsigned policy_index = 0; policy_index < 256u; ++policy_index) {
            std::snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpufreq/policy%u/scaling_governor",
                          policy_index);
            const std::string_view governor = probe::read_small_file(path, buffer);
            if (governor.empty()) {
                // Policy numbering is dense from zero, so the first gap is
                // the end. A host with no cpufreq at all leaves the seeds
                // untouched, which folds as "no governor policy".
                break;
            }
            governor_accumulator = probe::mix(governor_accumulator, probe::fold_bytes(governor));

            probe::SmallFileBuffer field_buffer{};
            std::snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpufreq/policy%u/energy_performance_preference",
                          policy_index);
            epp_accumulator =
                probe::mix(epp_accumulator, probe::fold_bytes(probe::read_small_file(path, field_buffer)));

            std::snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpufreq/policy%u/scaling_min_freq",
                          policy_index);
            min_freq = std::max(min_freq, probe::parse_unsigned(probe::read_small_file(path, field_buffer), 0u));

            std::snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpufreq/policy%u/scaling_max_freq",
                          policy_index);
            max_freq = std::max(max_freq, probe::parse_unsigned(probe::read_small_file(path, field_buffer), 0u));
        }
        facts.governor_digest = governor_accumulator;
        facts.epp_digest = epp_accumulator;
        facts.scaling_min_freq_khz = min_freq;
        facts.scaling_max_freq_khz = max_freq;
    }

    {
        probe::SmallFileBuffer enabled_buffer{};
        probe::SmallFileBuffer defrag_buffer{};
        const std::string_view enabled =
            probe::read_small_file("/sys/kernel/mm/transparent_hugepage/enabled", enabled_buffer);
        const std::string_view defrag =
            probe::read_small_file("/sys/kernel/mm/transparent_hugepage/defrag", defrag_buffer);
        facts.thp_digest = probe::mix(probe::fold_bytes(enabled), probe::fold_bytes(defrag));
    }

    {
        probe::SmallFileBuffer isolated_buffer{};
        probe::SmallFileBuffer nohz_buffer{};
        facts.isolated_cpu_digest =
            probe::fold_bytes(probe::read_small_file("/sys/devices/system/cpu/isolated", isolated_buffer));
        facts.nohz_full_digest =
            probe::fold_bytes(probe::read_small_file("/sys/devices/system/cpu/nohz_full", nohz_buffer));
    }

    {
        probe::SmallFileBuffer buffer{};
        const std::string_view control = probe::read_small_file("/sys/devices/system/cpu/smt/control", buffer);
        if (control == "off") {
            facts.smt_control = 1u;
        } else if (control == "on") {
            facts.smt_control = 2u;
        } else if (control == "forceoff") {
            facts.smt_control = 3u;
        } else {
            facts.smt_control = 0u;
        }
    }

    return facts;
}

// ── Projection into the cog schema ────────────────────────────────────
//
// The probed facts are re-expressed as the cog caps the rest of the tree
// already speaks, so the stable fold can call the projection CogMimic
// uses rather than inventing a second one over the same fields.

[[nodiscard]] inline cog::CpuSocketTargetCaps to_socket_caps(HostFacts const& facts) noexcept {
    cog::CpuCoreTargetCaps core{};
    core.base_clock_mhz = safety::Tagged<std::uint32_t, safety::source::Vendor>{
        static_cast<std::uint32_t>(facts.scaling_min_freq_khz / 1000u)};
    core.max_clock_mhz = safety::Tagged<std::uint32_t, safety::source::Vendor>{
        static_cast<std::uint32_t>(facts.scaling_max_freq_khz / 1000u)};
    core.l1d_bytes = safety::Tagged<std::uint32_t, safety::source::Vendor>{facts.l1d_bytes};
    core.l1i_bytes = safety::Tagged<std::uint32_t, safety::source::Vendor>{facts.l1i_bytes};
    core.l2_bytes = safety::Tagged<std::uint32_t, safety::source::Vendor>{facts.l2_bytes};
    core.features = facts.isa_features;

    cog::CpuSocketTargetCaps socket{};
    socket.core_count = safety::Tagged<std::uint16_t, safety::source::Vendor>{facts.physical_core_count};
    socket.thread_count = safety::Tagged<std::uint16_t, safety::source::Vendor>{facts.hw_thread_count};
    socket.l3_bytes = safety::Tagged<std::uint64_t, safety::source::Vendor>{facts.l3_total_bytes};
    socket.numa_node_count = safety::Tagged<std::uint8_t, safety::source::Vendor>{facts.numa_node_count};
    socket.representative_core = core;
    socket.features = facts.isa_features;
    return socket;
}

// ── The folds ─────────────────────────────────────────────────────────

[[nodiscard]] inline HardwareDigest fold_hardware(HostFacts const& facts) noexcept {
    namespace probe = fingerprint_detail;

    const cog::CpuSocketTargetCaps socket = to_socket_caps(facts);

    // The two projections CogMimic already publishes, called rather than
    // re-derived. Anything CogMimic treats as binary-compatibility-relevant
    // is verdict-relevant by construction.
    std::uint64_t accumulator = mimic::detail::caps_class_projection<cog::CogKind::CpuSocket>::fold(socket);
    accumulator = probe::mix(
        accumulator, mimic::detail::caps_class_projection<cog::CogKind::CpuCore>::fold(socket.representative_core));

    // Fields the caps projection deliberately leaves out because they do
    // not split a compiled binary, but that do split a measurement.
    accumulator = probe::mix(accumulator, probe::fold_bytes(facts.vendor_view()));
    accumulator = probe::mix(accumulator, probe::fold_bytes(facts.model_view()));
    accumulator = probe::mix(accumulator, static_cast<std::uint64_t>(facts.cpu_family));
    accumulator = probe::mix(accumulator, static_cast<std::uint64_t>(facts.cpu_model_number));
    accumulator = probe::mix(accumulator, static_cast<std::uint64_t>(facts.cpu_stepping));
    accumulator = probe::mix(accumulator, facts.microcode_revision);
    accumulator = probe::mix(accumulator, static_cast<std::uint64_t>(facts.l1d_bytes));
    accumulator = probe::mix(accumulator, static_cast<std::uint64_t>(facts.l1i_bytes));
    accumulator = probe::mix(accumulator, static_cast<std::uint64_t>(facts.cache_line_bytes));
    accumulator = probe::mix(accumulator, static_cast<std::uint64_t>(facts.hw_thread_count));
    accumulator = probe::mix(accumulator, static_cast<std::uint64_t>(facts.l3_instance_count));
    accumulator = probe::mix(accumulator, facts.numa_distance_digest);
    accumulator = probe::mix(accumulator, static_cast<std::uint64_t>(facts.page_size_bytes));
    accumulator = probe::mix(accumulator, static_cast<std::uint64_t>(facts.sve_vector_length_bits));

    // Zero is the sentinel for "no fingerprint", so a fold that lands there
    // is nudged off it rather than being mistaken for an absent probe.
    if (accumulator == 0u) {
        accumulator = 0x1ULL;
    }
    return HardwareDigest{accumulator};
}

[[nodiscard]] inline PolicyDigest fold_policy(HostFacts const& facts) noexcept {
    namespace probe = fingerprint_detail;

    std::uint64_t accumulator = 0xff51afd7ed558ccdULL;
    accumulator = probe::mix(accumulator, facts.governor_digest);
    accumulator = probe::mix(accumulator, facts.epp_digest);
    accumulator = probe::mix(accumulator, facts.scaling_min_freq_khz);
    accumulator = probe::mix(accumulator, facts.scaling_max_freq_khz);
    accumulator = probe::mix(accumulator, facts.thp_digest);
    accumulator = probe::mix(accumulator, facts.isolated_cpu_digest);
    accumulator = probe::mix(accumulator, facts.nohz_full_digest);
    accumulator = probe::mix(accumulator, static_cast<std::uint64_t>(facts.smt_control));

    if (accumulator == 0u) {
        accumulator = 0x1ULL;
    }
    return PolicyDigest{accumulator};
}

[[nodiscard]] inline HostFingerprint fold_fingerprint(HostFacts const& facts) noexcept {
    return HostFingerprint{.hardware = fold_hardware(facts), .policy = fold_policy(facts)};
}

[[nodiscard]] inline HostFingerprint probe_host_fingerprint() noexcept { return fold_fingerprint(probe_host_facts()); }

// Why a stored entry no longer applies. The caller wants the distinction:
// a hardware change means discard, a policy change means the operator
// retuned and the old file is still correct under its own key.
enum class FingerprintMatch : std::uint8_t {
    Exact = 0,
    PolicyChanged = 1,
    HardwareChanged = 2,
    Incomplete = 3,
};

[[nodiscard]] constexpr FingerprintMatch compare_fingerprints(HostFingerprint stored,
                                                              HostFingerprint current) noexcept {
    if (!stored.is_complete() || !current.is_complete()) {
        return FingerprintMatch::Incomplete;
    }
    if (stored.hardware != current.hardware) {
        return FingerprintMatch::HardwareChanged;
    }
    if (stored.policy != current.policy) {
        return FingerprintMatch::PolicyChanged;
    }
    return FingerprintMatch::Exact;
}

[[nodiscard]] constexpr std::string_view fingerprint_match_name(FingerprintMatch match) noexcept {
    switch (match) {
        case FingerprintMatch::Exact:
            return "Exact";
        case FingerprintMatch::PolicyChanged:
            return "PolicyChanged";
        case FingerprintMatch::HardwareChanged:
            return "HardwareChanged";
        case FingerprintMatch::Incomplete:
            return "Incomplete";
        // Not dead code: the value can arrive from a stored ledger, where
        // a byte outside the enum is a corrupt file rather than undefined
        // behaviour. Naming it is the fail-closed reading.
        default:
            return "<unknown FingerprintMatch>";
    }
}

namespace fingerprint_detail::self_test {

inline constexpr HostFingerprint s_unset{};
static_assert(!s_unset.is_complete());
static_assert(compare_fingerprints(s_unset, s_unset) == FingerprintMatch::Incomplete);

inline constexpr HostFingerprint s_left{HardwareDigest{7u}, PolicyDigest{11u}};
inline constexpr HostFingerprint s_same_hardware{HardwareDigest{7u}, PolicyDigest{13u}};
inline constexpr HostFingerprint s_other{HardwareDigest{9u}, PolicyDigest{11u}};
static_assert(s_left.is_complete());
static_assert(compare_fingerprints(s_left, s_left) == FingerprintMatch::Exact);
static_assert(compare_fingerprints(s_left, s_same_hardware) == FingerprintMatch::PolicyChanged);
static_assert(compare_fingerprints(s_left, s_other) == FingerprintMatch::HardwareChanged);

// A hardware change outranks a policy change: the answer a caller acts on
// must be the more destructive of the two.
inline constexpr HostFingerprint s_both{HardwareDigest{9u}, PolicyDigest{13u}};
static_assert(compare_fingerprints(s_left, s_both) == FingerprintMatch::HardwareChanged);

static_assert(fold_bytes("") != fold_bytes("performance"));
static_assert(fold_bytes("performance") != fold_bytes("powersave"));

// One mix() call is XOR-symmetric, so it cannot distinguish its two
// arguments on its own. The ordering guarantee lives in the CHAIN: each
// step finalizes before the next contribution is folded, so swapping two
// fields changes the digest. Every fold in this header is written as such
// a chain for exactly that reason, and this pair of assertions is what
// stops someone flattening one into a single xor-then-mix.
static_assert(mix(1u, 2u) == mix(2u, 1u), "a single mix is symmetric — do not rely on it for field ordering");
static_assert(mix(mix(0u, 1u), 2u) != mix(mix(0u, 2u), 1u), "the chained fold must be order-sensitive");

}  // namespace fingerprint_detail::self_test

}  // namespace crucible::ledger
