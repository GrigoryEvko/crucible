#pragma once

#include <crucible/cog/CogIdentity.h>
#include <crucible/cog/TargetCaps.h>
#include <fixy/Refined.h>
#include <fixy/Stale.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>
#include <foundation/reflect/EnumPins.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <type_traits>

namespace crucible::cog {

// Every enumerator in this file is frozen by underlying value, and the
// pin tables at the end of this file hold each value. The values name
// calibration rows that are meant to outlive the process, so a renumber
// would reinterpret a stored row. New atoms take the next free value
// and extend their pin table in the same change.
//
// ::foundation::reflect::enum_name gives the name of an enumerator. It
// reads the identifier that the enum declares, so no name table can
// fall out of step with the enum.
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

// The BLAS naming convention that every vendor library uses. T is
// transposed and N is not. The letter pair is ordered (A, B) for the
// product C = A · B, so Tt transposes both operands.
enum class TransposeMode : std::uint8_t {
    Nn = 0,
    Tn = 1,
    Nt = 2,
    Tt = 3,
};

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

// The catalog is short because a switch forwards traffic and does not
// compute.
enum class SwitchOpcode : std::uint16_t {
    PortForward = 0,
    AclMatch = 1,
    SharpReduce = 2,
    MulticastReplicate = 3,
};

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

enum class DramOpcode : std::uint16_t {
    ChannelRead = 0,
    ChannelWrite = 1,
    RowActivate = 2,
    BankRefresh = 3,
    Precharge = 4,
};

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
struct AreQuantilesOrdered {
    [[nodiscard]] constexpr bool operator()(const LatencyQuantiles& q) const noexcept {
        return q.p50_ns <= q.p99_ns && q.p99_ns <= q.p999_ns;
    }
};

inline constexpr AreQuantilesOrdered quantile_ordered{};

using OrderedLatencyQuantiles = ::fixy::Refined<quantile_ordered, LatencyQuantiles>;

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
    OrderedLatencyQuantiles latency = ::fixy::mint_refined<quantile_ordered>(LatencyQuantiles{});
    double throughput_per_sec = 0.0;

    CalibratedValue<std::uint16_t> sample_count{};
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

    CalibratedValue<std::span<const Entry>> entries{};

    ::fixy::Stale<double> calibration_age_seconds = ::fixy::Stale<double>::at_infinity(0.0);

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

static_assert(sizeof(CalibratedValue<std::uint16_t>) == sizeof(std::uint16_t));
static_assert(sizeof(CalibratedValue<std::span<const int>>) == sizeof(std::span<const int>));

static_assert(sizeof(::fixy::Stale<double>) <= 16);

using ::foundation::reflect::enum_pin;
using ::foundation::reflect::pin_enum;

inline constexpr std::array<enum_pin<SizeBucket>, 8> size_bucket_pins{{{"None", 0},
                                                                       {"S64", 64},
                                                                       {"S128", 128},
                                                                       {"S256", 256},
                                                                       {"S512", 512},
                                                                       {"S1024", 1024},
                                                                       {"S2048", 2048},
                                                                       {"S4096", 4096}}};
static_assert(pin_enum(size_bucket_pins), "SizeBucket drifted from size_bucket_pins.");

inline constexpr std::array<enum_pin<DtypeBucket>, 9> dtype_bucket_pins{{{"None", 0},
                                                                         {"Fp64", 1},
                                                                         {"Fp32", 2},
                                                                         {"Tf32", 3},
                                                                         {"Fp16", 4},
                                                                         {"Bf16", 5},
                                                                         {"Fp8", 6},
                                                                         {"Fp4", 7},
                                                                         {"Int8", 8}}};
static_assert(pin_enum(dtype_bucket_pins), "DtypeBucket drifted from dtype_bucket_pins.");

inline constexpr std::array<enum_pin<TransposeMode>, 4> transpose_mode_pins{
    {{"Nn", 0}, {"Tn", 1}, {"Nt", 2}, {"Tt", 3}}};
static_assert(pin_enum(transpose_mode_pins), "TransposeMode drifted from transpose_mode_pins.");

inline constexpr std::array<enum_pin<MessageSizeBucket>, 7> message_size_bucket_pins{{{"None", 0},
                                                                                      {"M64B", 64},
                                                                                      {"M1K", 1024},
                                                                                      {"M16K", 16384},
                                                                                      {"M256K", 262144},
                                                                                      {"M4M", 4194304},
                                                                                      {"M64M", 67108864}}};
static_assert(pin_enum(message_size_bucket_pins), "MessageSizeBucket drifted from message_size_bucket_pins.");

inline constexpr std::array<enum_pin<GpuOpcode>, 13> gpu_opcode_pins{{{"GemmPlain", 0},
                                                                      {"GemmFused", 1},
                                                                      {"Sdpa", 2},
                                                                      {"Conv2D", 3},
                                                                      {"AllReduceRing", 4},
                                                                      {"AllReduceTree", 5},
                                                                      {"AllGather", 6},
                                                                      {"NvlinkP2pRead", 7},
                                                                      {"NvlinkP2pWrite", 8},
                                                                      {"PciePeer", 9},
                                                                      {"KernelLaunch", 10},
                                                                      {"DoorbellRing", 11},
                                                                      {"EventQuery", 12}}};
static_assert(pin_enum(gpu_opcode_pins), "GpuOpcode drifted from gpu_opcode_pins.");

inline constexpr std::array<enum_pin<NicOpcode>, 15> nic_opcode_pins{{{"RdmaWrite", 0},
                                                                      {"RdmaSend", 1},
                                                                      {"RdmaRead", 2},
                                                                      {"CompletionPoll", 3},
                                                                      {"QpCreate", 4},
                                                                      {"QpDestroy", 5},
                                                                      {"MrRegister", 6},
                                                                      {"MrDeregister", 7},
                                                                      {"DoorbellRing", 8},
                                                                      {"TcpSend", 9},
                                                                      {"TcpRecv", 10},
                                                                      {"AfXdpEnqueue", 11},
                                                                      {"AfXdpDequeue", 12},
                                                                      {"GpuDirectWrite", 13},
                                                                      {"GpuDirectRead", 14}}};
static_assert(pin_enum(nic_opcode_pins), "NicOpcode drifted from nic_opcode_pins.");

inline constexpr std::array<enum_pin<SwitchOpcode>, 4> switch_opcode_pins{
    {{"PortForward", 0}, {"AclMatch", 1}, {"SharpReduce", 2}, {"MulticastReplicate", 3}}};
static_assert(pin_enum(switch_opcode_pins), "SwitchOpcode drifted from switch_opcode_pins.");

inline constexpr std::array<enum_pin<CpuOpcode>, 10> cpu_opcode_pins{{{"Memcpy", 0},
                                                                      {"Vfma", 1},
                                                                      {"AvxLoad", 2},
                                                                      {"AvxStore", 3},
                                                                      {"ContextSwitch", 4},
                                                                      {"AtomicCas", 5},
                                                                      {"MutexLock", 6},
                                                                      {"MutexUnlock", 7},
                                                                      {"FutexWait", 8},
                                                                      {"Syscall", 9}}};
static_assert(pin_enum(cpu_opcode_pins), "CpuOpcode drifted from cpu_opcode_pins.");

inline constexpr std::array<enum_pin<DramOpcode>, 5> dram_opcode_pins{
    {{"ChannelRead", 0}, {"ChannelWrite", 1}, {"RowActivate", 2}, {"BankRefresh", 3}, {"Precharge", 4}}};
static_assert(pin_enum(dram_opcode_pins), "DramOpcode drifted from dram_opcode_pins.");

// A pin table holds a value, not a width. Widening an underlying type
// changes the size of every entry that holds the enum by value.
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

// An entry is not trivially copyable, because its refined latency
// refuses a byte copy, so no code builds an entry from raw bytes.
// Standard layout keeps offsetof valid on every field.
static_assert(!std::is_trivially_copyable_v<OpcodeLatencyEntry<CogKind::Gpu>>);
static_assert(std::is_standard_layout_v<OpcodeLatencyEntry<CogKind::Gpu>>);
static_assert(std::is_standard_layout_v<OpcodeLatencyEntry<CogKind::NicPort>>);

}  // namespace detail::opcode_latency_self_test

}  // namespace crucible::cog
