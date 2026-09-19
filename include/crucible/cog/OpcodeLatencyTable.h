#pragma once

#include <crucible/cog/CogIdentity.h>
#include <crucible/cog/TargetCaps.h>
#include <crucible/safety/_Refined.h>
#include <crucible/safety/Stale.h>
#include <crucible/safety/Tagged.h>

#include <cstdint>
#include <meta>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>

namespace crucible::cog {

// Every enumerator in this file is frozen by underlying value. A table
// is serialized into cached snapshots that outlive the process, so
// renumbering an atom silently reinterprets every snapshot that carries
// it. New atoms take the next free value.
//
// A GEMM dimension bucket, one each for M, N and K. The underlying
// value is the bucket size in elements, so a call site can do
// arithmetic on it directly.
enum class SizeBucket : std::uint16_t {
    None = 0,
    S64 = 64,
    S128 = 128,
    S256 = 256,
    S512 = 512,
    S1024 = 1024,
    S2048 = 2048,
    S4096 = 4096,
};
inline constexpr std::size_t size_bucket_count = std::meta::enumerators_of(^^SizeBucket).size();

[[nodiscard]] constexpr std::string_view size_bucket_name(SizeBucket B) noexcept {
    switch (B) {
        case SizeBucket::None:
            return "None";
        case SizeBucket::S64:
            return "S64";
        case SizeBucket::S128:
            return "S128";
        case SizeBucket::S256:
            return "S256";
        case SizeBucket::S512:
            return "S512";
        case SizeBucket::S1024:
            return "S1024";
        case SizeBucket::S2048:
            return "S2048";
        case SizeBucket::S4096:
            return "S4096";
        default:
            return std::string_view{"<unknown SizeBucket>"};
    }
}

// A plain ordinal, unlike SizeBucket. The value carries no numeric
// meaning.
enum class DtypeBucket : std::uint8_t {
    None = 0,
    Fp64 = 1,
    Fp32 = 2,
    Tf32 = 3,
    Fp16 = 4,
    Bf16 = 5,
    Fp8 = 6,
    Fp4 = 7,
    Int8 = 8,
};
inline constexpr std::size_t dtype_bucket_count = std::meta::enumerators_of(^^DtypeBucket).size();

[[nodiscard]] constexpr std::string_view dtype_bucket_name(DtypeBucket B) noexcept {
    switch (B) {
        case DtypeBucket::None:
            return "None";
        case DtypeBucket::Fp64:
            return "Fp64";
        case DtypeBucket::Fp32:
            return "Fp32";
        case DtypeBucket::Tf32:
            return "Tf32";
        case DtypeBucket::Fp16:
            return "Fp16";
        case DtypeBucket::Bf16:
            return "Bf16";
        case DtypeBucket::Fp8:
            return "Fp8";
        case DtypeBucket::Fp4:
            return "Fp4";
        case DtypeBucket::Int8:
            return "Int8";
        default:
            return std::string_view{"<unknown DtypeBucket>"};
    }
}

// The BLAS naming convention that every vendor library uses. T is
// transposed and N is not. The letter pair is ordered (A, B) for the
// product C = A · B, so Tt transposes both operands.
enum class TransposeMode : std::uint8_t {
    Nn = 0,
    Tn = 1,
    Nt = 2,
    Tt = 3,
};
inline constexpr std::size_t transpose_mode_count = std::meta::enumerators_of(^^TransposeMode).size();

[[nodiscard]] constexpr std::string_view transpose_mode_name(TransposeMode M) noexcept {
    switch (M) {
        case TransposeMode::Nn:
            return "Nn";
        case TransposeMode::Tn:
            return "Tn";
        case TransposeMode::Nt:
            return "Nt";
        case TransposeMode::Tt:
            return "Tt";
        default:
            return std::string_view{"<unknown TransposeMode>"};
    }
}

// The underlying value is the bucket size in bytes, the same
// convention SizeBucket follows.
enum class MessageSizeBucket : std::uint32_t {
    None = 0,
    M64B = 64,
    M1K = 1024,
    M16K = 16384,
    M256K = 262144,
    M4M = 4194304,
    M64M = 67108864,
};
inline constexpr std::size_t message_size_bucket_count = std::meta::enumerators_of(^^MessageSizeBucket).size();

[[nodiscard]] constexpr std::string_view message_size_bucket_name(MessageSizeBucket B) noexcept {
    switch (B) {
        case MessageSizeBucket::None:
            return "None";
        case MessageSizeBucket::M64B:
            return "M64B";
        case MessageSizeBucket::M1K:
            return "M1K";
        case MessageSizeBucket::M16K:
            return "M16K";
        case MessageSizeBucket::M256K:
            return "M256K";
        case MessageSizeBucket::M4M:
            return "M4M";
        case MessageSizeBucket::M64M:
            return "M64M";
        default:
            return std::string_view{"<unknown MessageSizeBucket>"};
    }
}

enum class GpuOpcode : std::uint16_t {
    GemmPlain = 0,  // dense GEMM, C = A · B
    GemmFused = 1,  // GEMM with a bias or activation epilogue
    Sdpa = 2,  // scaled-dot-product attention
    Conv2D = 3,  // dense 2D convolution
    AllReduceRing = 4,
    AllReduceTree = 5,
    AllGather = 6,
    NvlinkP2pRead = 7,
    NvlinkP2pWrite = 8,
    PciePeer = 9,  // peer DMA across root complexes
    KernelLaunch = 10,
    DoorbellRing = 11,
    EventQuery = 12,
};
inline constexpr std::size_t gpu_opcode_count = std::meta::enumerators_of(^^GpuOpcode).size();

// A network opcode is measured once per message-size bucket, because
// the latency and bandwidth of one operation vary by orders of
// magnitude across the size range.
enum class NicOpcode : std::uint16_t {
    RdmaWrite = 0,
    RdmaSend = 1,
    RdmaRead = 2,
    CompletionPoll = 3,
    QpCreate = 4,
    QpDestroy = 5,
    MrRegister = 6,
    MrDeregister = 7,
    DoorbellRing = 8,
    TcpSend = 9,
    TcpRecv = 10,
    AfXdpEnqueue = 11,
    AfXdpDequeue = 12,
    GpuDirectWrite = 13,
    GpuDirectRead = 14,
};
inline constexpr std::size_t nic_opcode_count = std::meta::enumerators_of(^^NicOpcode).size();

// The catalog is short because a switch forwards traffic and does not
// compute.
enum class SwitchOpcode : std::uint16_t {
    PortForward = 0,
    AclMatch = 1,
    SharpReduce = 2,
    MulticastReplicate = 3,
};
inline constexpr std::size_t switch_opcode_count = std::meta::enumerators_of(^^SwitchOpcode).size();

enum class CpuOpcode : std::uint16_t {
    Memcpy = 0,
    Vfma = 1,
    AvxLoad = 2,
    AvxStore = 3,
    ContextSwitch = 4,
    AtomicCas = 5,
    MutexLock = 6,
    MutexUnlock = 7,
    FutexWait = 8,
    Syscall = 9,
};
inline constexpr std::size_t cpu_opcode_count = std::meta::enumerators_of(^^CpuOpcode).size();

enum class DramOpcode : std::uint16_t {
    ChannelRead = 0,
    ChannelWrite = 1,
    RowActivate = 2,
    BankRefresh = 3,
    Precharge = 4,
};
inline constexpr std::size_t dram_opcode_count = std::meta::enumerators_of(^^DramOpcode).size();

[[nodiscard]] constexpr std::string_view gpu_opcode_name(GpuOpcode O) noexcept {
    switch (O) {
        case GpuOpcode::GemmPlain:
            return "GemmPlain";
        case GpuOpcode::GemmFused:
            return "GemmFused";
        case GpuOpcode::Sdpa:
            return "Sdpa";
        case GpuOpcode::Conv2D:
            return "Conv2D";
        case GpuOpcode::AllReduceRing:
            return "AllReduceRing";
        case GpuOpcode::AllReduceTree:
            return "AllReduceTree";
        case GpuOpcode::AllGather:
            return "AllGather";
        case GpuOpcode::NvlinkP2pRead:
            return "NvlinkP2pRead";
        case GpuOpcode::NvlinkP2pWrite:
            return "NvlinkP2pWrite";
        case GpuOpcode::PciePeer:
            return "PciePeer";
        case GpuOpcode::KernelLaunch:
            return "KernelLaunch";
        case GpuOpcode::DoorbellRing:
            return "DoorbellRing";
        case GpuOpcode::EventQuery:
            return "EventQuery";
        default:
            return std::string_view{"<unknown GpuOpcode>"};
    }
}

[[nodiscard]] constexpr std::string_view nic_opcode_name(NicOpcode O) noexcept {
    switch (O) {
        case NicOpcode::RdmaWrite:
            return "RdmaWrite";
        case NicOpcode::RdmaSend:
            return "RdmaSend";
        case NicOpcode::RdmaRead:
            return "RdmaRead";
        case NicOpcode::CompletionPoll:
            return "CompletionPoll";
        case NicOpcode::QpCreate:
            return "QpCreate";
        case NicOpcode::QpDestroy:
            return "QpDestroy";
        case NicOpcode::MrRegister:
            return "MrRegister";
        case NicOpcode::MrDeregister:
            return "MrDeregister";
        case NicOpcode::DoorbellRing:
            return "DoorbellRing";
        case NicOpcode::TcpSend:
            return "TcpSend";
        case NicOpcode::TcpRecv:
            return "TcpRecv";
        case NicOpcode::AfXdpEnqueue:
            return "AfXdpEnqueue";
        case NicOpcode::AfXdpDequeue:
            return "AfXdpDequeue";
        case NicOpcode::GpuDirectWrite:
            return "GpuDirectWrite";
        case NicOpcode::GpuDirectRead:
            return "GpuDirectRead";
        default:
            return std::string_view{"<unknown NicOpcode>"};
    }
}

[[nodiscard]] constexpr std::string_view switch_opcode_name(SwitchOpcode O) noexcept {
    switch (O) {
        case SwitchOpcode::PortForward:
            return "PortForward";
        case SwitchOpcode::AclMatch:
            return "AclMatch";
        case SwitchOpcode::SharpReduce:
            return "SharpReduce";
        case SwitchOpcode::MulticastReplicate:
            return "MulticastReplicate";
        default:
            return std::string_view{"<unknown SwitchOpcode>"};
    }
}

[[nodiscard]] constexpr std::string_view cpu_opcode_name(CpuOpcode O) noexcept {
    switch (O) {
        case CpuOpcode::Memcpy:
            return "Memcpy";
        case CpuOpcode::Vfma:
            return "Vfma";
        case CpuOpcode::AvxLoad:
            return "AvxLoad";
        case CpuOpcode::AvxStore:
            return "AvxStore";
        case CpuOpcode::ContextSwitch:
            return "ContextSwitch";
        case CpuOpcode::AtomicCas:
            return "AtomicCas";
        case CpuOpcode::MutexLock:
            return "MutexLock";
        case CpuOpcode::MutexUnlock:
            return "MutexUnlock";
        case CpuOpcode::FutexWait:
            return "FutexWait";
        case CpuOpcode::Syscall:
            return "Syscall";
        default:
            return std::string_view{"<unknown CpuOpcode>"};
    }
}

[[nodiscard]] constexpr std::string_view dram_opcode_name(DramOpcode O) noexcept {
    switch (O) {
        case DramOpcode::ChannelRead:
            return "ChannelRead";
        case DramOpcode::ChannelWrite:
            return "ChannelWrite";
        case DramOpcode::RowActivate:
            return "RowActivate";
        case DramOpcode::BankRefresh:
            return "BankRefresh";
        case DramOpcode::Precharge:
            return "Precharge";
        default:
            return std::string_view{"<unknown DramOpcode>"};
    }
}

// Nanoseconds. The type stays a plain aggregate so it can be loaded
// straight from disk. The ordering invariant lives one level up, in
// the refined alias below.
struct LatencyQuantiles {
    std::uint32_t p50_ns = 0;
    std::uint32_t p99_ns = 0;
    std::uint32_t p999_ns = 0;

    [[nodiscard]] friend constexpr bool operator==(const LatencyQuantiles&, const LatencyQuantiles&) noexcept = default;
};

static_assert(sizeof(LatencyQuantiles) == 3 * sizeof(std::uint32_t),
              "LatencyQuantiles has gained padding or a field. The serialized "
              "byte layout no longer matches what readers expect.");
static_assert(std::is_trivially_copyable_v<LatencyQuantiles>);
static_assert(std::is_standard_layout_v<LatencyQuantiles>);

// Quantiles of one measurement are monotonic. A triple that is not
// came from corrupt storage, from an import under a different
// histogram convention, or from a calibrator that mislabelled the
// fields, so the refinement rejects it at construction.
inline constexpr auto quantile_ordered = [](const LatencyQuantiles& q) constexpr noexcept {
    return q.p50_ns <= q.p99_ns && q.p99_ns <= q.p999_ns;
};

using OrderedLatencyQuantiles = safety::Refined<quantile_ordered, LatencyQuantiles>;

static_assert(sizeof(OrderedLatencyQuantiles) == sizeof(LatencyQuantiles),
              "The ordering refinement must add no storage to the triple it "
              "refines.");

// The primary template stays undefined. A kind with no opcode catalog
// then fails at the point of use rather than binding to a wrong one.
template <CogKind K>
struct opcodes_for;

template <>
struct opcodes_for<CogKind::Gpu> {
    using type = GpuOpcode;
};
template <>
struct opcodes_for<CogKind::CpuCore> {
    using type = CpuOpcode;
};
template <>
struct opcodes_for<CogKind::CpuSocket> {
    using type = CpuOpcode;
};
template <>
struct opcodes_for<CogKind::NicPort> {
    using type = NicOpcode;
};
template <>
struct opcodes_for<CogKind::NvSwitch> {
    using type = SwitchOpcode;
};
template <>
struct opcodes_for<CogKind::DramChannel> {
    using type = DramOpcode;
};

template <CogKind K>
using opcodes_for_t = typename opcodes_for<K>::type;

template <CogKind K>
concept HasOpcodeTable = HasCaps<K> && requires { typename opcodes_for<K>::type; };

// One row of the per-Cog latency table.
//
// The four bucket fields form a cross-product key, and an opcode uses
// only the dimensions that apply to it. An unused dimension is
// explicitly None rather than left to chance, so two rows for the same
// opcode never collide on a field nobody set.
//
// latency_cycles repeats the p50 in cycles because the dispatch budget
// is counted in cycles, where nanoseconds are too coarse to divide.
//
// throughput_per_sec counts operations per second for a compute opcode
// and bytes per second for a transfer opcode.
//
// A sample_count of zero means the row was never measured, and a
// consumer reading one falls back to the vendor specification.
template <CogKind K>
    requires HasOpcodeTable<K>
struct OpcodeLatencyEntry {
    using OpcodeId = opcodes_for_t<K>;

    OpcodeId opcode{};
    SizeBucket size_bucket = SizeBucket::None;
    DtypeBucket dtype_bucket = DtypeBucket::None;
    TransposeMode transpose_mode = TransposeMode::Nn;
    MessageSizeBucket message_size_bucket = MessageSizeBucket::None;

    std::uint32_t latency_cycles = 0;
    OrderedLatencyQuantiles latency{LatencyQuantiles{}};
    double throughput_per_sec = 0.0;

    safety::Tagged<std::uint16_t, safety::source::Calibrated> sample_count{std::uint16_t{0}};
};

// One table holds the calibrated rows for one Cog.
//
// A per-Cog table rather than one table per chip model: manufacturing
// variation, firmware revision and thermal headroom make two parts of
// the same model diverge measurably on identical work.
//
// The span borrows arena-owned storage, which must outlive the table.
// An empty span means the table was never filled in, and a consumer
// reading one falls back to vendor-specification defaults.
//
// calibration_age_seconds carries wall-clock seconds since the
// measurement, and its staleness grade counts update cycles since. A
// table that has never been calibrated starts at infinite staleness,
// not zero. Zero reads as "measured this cycle", which would convince
// the drift detector that a table it has never seen is fresh, and no
// first measurement would ever be requested.
//
// A lookup is a linear scan over the span. A hash index would cost
// more in cache lines touched than the scan costs, because a table
// holds a few hundred rows at most and a lookup happens about once per
// iteration.
template <CogKind K>
    requires HasOpcodeTable<K>
struct OpcodeLatencyTable {
    using Entry = OpcodeLatencyEntry<K>;
    using OpcodeId = opcodes_for_t<K>;

    safety::Tagged<std::span<const Entry>, safety::source::Calibrated> entries{std::span<const Entry>{}};

    safety::Stale<double> calibration_age_seconds = safety::Stale<double>::at_infinity(0.0);

    [[nodiscard]] constexpr std::optional<Entry> lookup_by_opcode(OpcodeId target) const noexcept {
        const auto& span_view = entries.value();
        for (const Entry& e : span_view) {
            if (e.opcode == target) return e;
        }
        return std::nullopt;
    }

    [[nodiscard]] constexpr std::optional<Entry>
    latency_for_size_bucket(OpcodeId target_opcode, SizeBucket target_size, DtypeBucket target_dtype,
                            TransposeMode target_transpose = TransposeMode::Nn,
                            MessageSizeBucket target_msgsz = MessageSizeBucket::None) const noexcept {
        const auto& span_view = entries.value();
        for (const Entry& e : span_view) {
            if (e.opcode == target_opcode && e.size_bucket == target_size && e.dtype_bucket == target_dtype
                && e.transpose_mode == target_transpose && e.message_size_bucket == target_msgsz)
                return e;
        }
        return std::nullopt;
    }

    [[nodiscard]] constexpr double throughput_envelope(OpcodeId target_opcode) const noexcept {
        const auto& span_view = entries.value();
        double sum = 0.0;
        for (const Entry& e : span_view) {
            if (e.opcode == target_opcode) sum += e.throughput_per_sec;
        }
        return sum;
    }

    [[nodiscard]] constexpr std::size_t size() const noexcept { return entries.value().size(); }

    [[nodiscard]] constexpr bool empty() const noexcept { return entries.value().empty(); }
};

namespace detail::opcode_latency_self_test {

static_assert(sizeof(OrderedLatencyQuantiles) == sizeof(LatencyQuantiles));

static_assert(sizeof(safety::Tagged<std::uint16_t, safety::source::Calibrated>) == sizeof(std::uint16_t));
static_assert(sizeof(safety::Tagged<std::span<const int>, safety::source::Calibrated>) == sizeof(std::span<const int>));

static_assert(sizeof(safety::Stale<double>) <= 16);

[[nodiscard]] consteval bool every_size_bucket_has_name() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^SizeBucket));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (size_bucket_name([:en:]) == std::string_view{"<unknown SizeBucket>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_size_bucket_has_name(), "size_bucket_name() switch missing arm for at least one SizeBucket "
                                            "atom.");

[[nodiscard]] consteval bool every_dtype_bucket_has_name() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^DtypeBucket));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (dtype_bucket_name([:en:]) == std::string_view{"<unknown DtypeBucket>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_dtype_bucket_has_name());

[[nodiscard]] consteval bool every_transpose_mode_has_name() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^TransposeMode));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (transpose_mode_name([:en:]) == std::string_view{"<unknown TransposeMode>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_transpose_mode_has_name());

[[nodiscard]] consteval bool every_message_size_bucket_has_name() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^MessageSizeBucket));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (message_size_bucket_name([:en:]) == std::string_view{"<unknown MessageSizeBucket>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_message_size_bucket_has_name());

[[nodiscard]] consteval bool every_gpu_opcode_has_name() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^GpuOpcode));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (gpu_opcode_name([:en:]) == std::string_view{"<unknown GpuOpcode>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_gpu_opcode_has_name());

[[nodiscard]] consteval bool every_nic_opcode_has_name() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^NicOpcode));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (nic_opcode_name([:en:]) == std::string_view{"<unknown NicOpcode>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_nic_opcode_has_name());

[[nodiscard]] consteval bool every_switch_opcode_has_name() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^SwitchOpcode));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (switch_opcode_name([:en:]) == std::string_view{"<unknown SwitchOpcode>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_switch_opcode_has_name());

[[nodiscard]] consteval bool every_cpu_opcode_has_name() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^CpuOpcode));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (cpu_opcode_name([:en:]) == std::string_view{"<unknown CpuOpcode>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_cpu_opcode_has_name());

[[nodiscard]] consteval bool every_dram_opcode_has_name() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^DramOpcode));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (dram_opcode_name([:en:]) == std::string_view{"<unknown DramOpcode>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_dram_opcode_has_name());

static_assert(static_cast<std::uint16_t>(SizeBucket::None) == 0,
              "SizeBucket::None has moved off its frozen value. Every cached "
              "snapshot that carries this atom now decodes to the wrong bucket.");
static_assert(static_cast<std::uint16_t>(SizeBucket::S64) == 64);
static_assert(static_cast<std::uint16_t>(SizeBucket::S128) == 128);
static_assert(static_cast<std::uint16_t>(SizeBucket::S256) == 256);
static_assert(static_cast<std::uint16_t>(SizeBucket::S512) == 512);
static_assert(static_cast<std::uint16_t>(SizeBucket::S1024) == 1024);
static_assert(static_cast<std::uint16_t>(SizeBucket::S2048) == 2048);
static_assert(static_cast<std::uint16_t>(SizeBucket::S4096) == 4096);

static_assert(static_cast<std::uint8_t>(DtypeBucket::None) == 0);
static_assert(static_cast<std::uint8_t>(DtypeBucket::Fp64) == 1);
static_assert(static_cast<std::uint8_t>(DtypeBucket::Fp32) == 2);
static_assert(static_cast<std::uint8_t>(DtypeBucket::Tf32) == 3);
static_assert(static_cast<std::uint8_t>(DtypeBucket::Fp16) == 4);
static_assert(static_cast<std::uint8_t>(DtypeBucket::Bf16) == 5);
static_assert(static_cast<std::uint8_t>(DtypeBucket::Fp8) == 6);
static_assert(static_cast<std::uint8_t>(DtypeBucket::Fp4) == 7);
static_assert(static_cast<std::uint8_t>(DtypeBucket::Int8) == 8);

static_assert(static_cast<std::uint8_t>(TransposeMode::Nn) == 0);
static_assert(static_cast<std::uint8_t>(TransposeMode::Tn) == 1);
static_assert(static_cast<std::uint8_t>(TransposeMode::Nt) == 2);
static_assert(static_cast<std::uint8_t>(TransposeMode::Tt) == 3);

static_assert(static_cast<std::uint32_t>(MessageSizeBucket::None) == 0);
static_assert(static_cast<std::uint32_t>(MessageSizeBucket::M64B) == 64);
static_assert(static_cast<std::uint32_t>(MessageSizeBucket::M1K) == 1024);
static_assert(static_cast<std::uint32_t>(MessageSizeBucket::M16K) == 16384);
static_assert(static_cast<std::uint32_t>(MessageSizeBucket::M256K) == 262144);
static_assert(static_cast<std::uint32_t>(MessageSizeBucket::M4M) == 4194304);
static_assert(static_cast<std::uint32_t>(MessageSizeBucket::M64M) == 67108864);

static_assert(static_cast<std::uint16_t>(GpuOpcode::GemmPlain) == 0);
static_assert(static_cast<std::uint16_t>(GpuOpcode::GemmFused) == 1);
static_assert(static_cast<std::uint16_t>(GpuOpcode::Sdpa) == 2);
static_assert(static_cast<std::uint16_t>(GpuOpcode::Conv2D) == 3);
static_assert(static_cast<std::uint16_t>(GpuOpcode::AllReduceRing) == 4);
static_assert(static_cast<std::uint16_t>(GpuOpcode::AllReduceTree) == 5);
static_assert(static_cast<std::uint16_t>(GpuOpcode::AllGather) == 6);
static_assert(static_cast<std::uint16_t>(GpuOpcode::NvlinkP2pRead) == 7);
static_assert(static_cast<std::uint16_t>(GpuOpcode::NvlinkP2pWrite) == 8);
static_assert(static_cast<std::uint16_t>(GpuOpcode::PciePeer) == 9);
static_assert(static_cast<std::uint16_t>(GpuOpcode::KernelLaunch) == 10);
static_assert(static_cast<std::uint16_t>(GpuOpcode::DoorbellRing) == 11);
static_assert(static_cast<std::uint16_t>(GpuOpcode::EventQuery) == 12);

static_assert(static_cast<std::uint16_t>(NicOpcode::RdmaWrite) == 0);
static_assert(static_cast<std::uint16_t>(NicOpcode::RdmaSend) == 1);
static_assert(static_cast<std::uint16_t>(NicOpcode::RdmaRead) == 2);
static_assert(static_cast<std::uint16_t>(NicOpcode::CompletionPoll) == 3);
static_assert(static_cast<std::uint16_t>(NicOpcode::QpCreate) == 4);
static_assert(static_cast<std::uint16_t>(NicOpcode::QpDestroy) == 5);
static_assert(static_cast<std::uint16_t>(NicOpcode::MrRegister) == 6);
static_assert(static_cast<std::uint16_t>(NicOpcode::MrDeregister) == 7);
static_assert(static_cast<std::uint16_t>(NicOpcode::DoorbellRing) == 8);
static_assert(static_cast<std::uint16_t>(NicOpcode::TcpSend) == 9);
static_assert(static_cast<std::uint16_t>(NicOpcode::TcpRecv) == 10);
static_assert(static_cast<std::uint16_t>(NicOpcode::AfXdpEnqueue) == 11);
static_assert(static_cast<std::uint16_t>(NicOpcode::AfXdpDequeue) == 12);
static_assert(static_cast<std::uint16_t>(NicOpcode::GpuDirectWrite) == 13);
static_assert(static_cast<std::uint16_t>(NicOpcode::GpuDirectRead) == 14);

static_assert(static_cast<std::uint16_t>(SwitchOpcode::PortForward) == 0);
static_assert(static_cast<std::uint16_t>(SwitchOpcode::AclMatch) == 1);
static_assert(static_cast<std::uint16_t>(SwitchOpcode::SharpReduce) == 2);
static_assert(static_cast<std::uint16_t>(SwitchOpcode::MulticastReplicate) == 3);

static_assert(static_cast<std::uint16_t>(CpuOpcode::Memcpy) == 0);
static_assert(static_cast<std::uint16_t>(CpuOpcode::Vfma) == 1);
static_assert(static_cast<std::uint16_t>(CpuOpcode::AvxLoad) == 2);
static_assert(static_cast<std::uint16_t>(CpuOpcode::AvxStore) == 3);
static_assert(static_cast<std::uint16_t>(CpuOpcode::ContextSwitch) == 4);
static_assert(static_cast<std::uint16_t>(CpuOpcode::AtomicCas) == 5);
static_assert(static_cast<std::uint16_t>(CpuOpcode::MutexLock) == 6);
static_assert(static_cast<std::uint16_t>(CpuOpcode::MutexUnlock) == 7);
static_assert(static_cast<std::uint16_t>(CpuOpcode::FutexWait) == 8);
static_assert(static_cast<std::uint16_t>(CpuOpcode::Syscall) == 9);

static_assert(static_cast<std::uint16_t>(DramOpcode::ChannelRead) == 0);
static_assert(static_cast<std::uint16_t>(DramOpcode::ChannelWrite) == 1);
static_assert(static_cast<std::uint16_t>(DramOpcode::RowActivate) == 2);
static_assert(static_cast<std::uint16_t>(DramOpcode::BankRefresh) == 3);
static_assert(static_cast<std::uint16_t>(DramOpcode::Precharge) == 4);

// Widening an underlying type re-shapes every entry that holds the
// enum by value, and with it the serialized byte image.
static_assert(std::is_same_v<std::underlying_type_t<SizeBucket>, std::uint16_t>);
static_assert(std::is_same_v<std::underlying_type_t<DtypeBucket>, std::uint8_t>);
static_assert(std::is_same_v<std::underlying_type_t<TransposeMode>, std::uint8_t>);
static_assert(std::is_same_v<std::underlying_type_t<MessageSizeBucket>, std::uint32_t>);
static_assert(std::is_same_v<std::underlying_type_t<GpuOpcode>, std::uint16_t>);
static_assert(std::is_same_v<std::underlying_type_t<NicOpcode>, std::uint16_t>);
static_assert(std::is_same_v<std::underlying_type_t<SwitchOpcode>, std::uint16_t>);
static_assert(std::is_same_v<std::underlying_type_t<CpuOpcode>, std::uint16_t>);
static_assert(std::is_same_v<std::underlying_type_t<DramOpcode>, std::uint16_t>);

static_assert(std::is_same_v<opcodes_for_t<CogKind::Gpu>, GpuOpcode>);
static_assert(std::is_same_v<opcodes_for_t<CogKind::CpuCore>, CpuOpcode>);
static_assert(std::is_same_v<opcodes_for_t<CogKind::CpuSocket>, CpuOpcode>);
static_assert(std::is_same_v<opcodes_for_t<CogKind::NicPort>, NicOpcode>);
static_assert(std::is_same_v<opcodes_for_t<CogKind::NvSwitch>, SwitchOpcode>);
static_assert(std::is_same_v<opcodes_for_t<CogKind::DramChannel>, DramOpcode>);

static_assert(HasOpcodeTable<CogKind::Gpu>);
static_assert(HasOpcodeTable<CogKind::CpuCore>);
static_assert(HasOpcodeTable<CogKind::CpuSocket>);
static_assert(HasOpcodeTable<CogKind::NicPort>);
static_assert(HasOpcodeTable<CogKind::NvSwitch>);
static_assert(HasOpcodeTable<CogKind::DramChannel>);
static_assert(!HasOpcodeTable<CogKind::PsuRail>);
static_assert(!HasOpcodeTable<CogKind::BmcSensor>);
static_assert(!HasOpcodeTable<CogKind::Datacenter>);

// A table is written to disk and shipped over the wire as raw bytes.
// Standard layout is what keeps that byte image stable.
static_assert(std::is_standard_layout_v<LatencyQuantiles>);
static_assert(std::is_standard_layout_v<OpcodeLatencyEntry<CogKind::Gpu>>);
static_assert(std::is_standard_layout_v<OpcodeLatencyEntry<CogKind::NicPort>>);

}  // namespace detail::opcode_latency_self_test

}  // namespace crucible::cog
