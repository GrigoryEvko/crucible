#pragma once

// ── crucible::cog::OpcodeLatencyTable — per-Cog calibrated tables ───
//
// GAPS-187.  Per misc/03_05_2026_networking.md §3.5 and CRUCIBLE.md L2.
// Each schedulable substrate Cog publishes a typed opcode catalog and
// per-opcode calibrated latency / throughput envelope.  Two consumers
// drive the design:
//
//   * Mimic's MAP-Elites kernel search (GAPS-188 mimic/CogMimic.h)
//     reads the latency-per-opcode envelope when emitting kernel
//     variants — variants that exceed the calibrated p99 budget are
//     pruned from the archive.
//   * The discrete-search partition optimizer (Forge §25.6 + the Z3-
//     removal cost surface, GAPS-810) reads the envelope when picking
//     5D-parallelism placement: comparing GEMM-cost vs all_reduce-cost
//     vs PCIe-peer-cost on the calibrated surface decides whether a
//     given partition fits the per-Cog budget.
//
// Per-class generic tables ("all H100s have GEMM latency X") are
// insufficient: manufacturing variation, firmware revision, and
// thermal headroom produce per-Cog divergence of 5-20% on identical
// workloads.  Calibration runs at startup (GAPS-196 cog/Calibrate.h)
// AND on Augur-detected drift (#1212 audit / Augur recommendations);
// Stale<> staleness grades drive the recalibration trigger.
//
// ── Composition (mirrors cog/TargetCaps.h §I structure) ─────────────
//
// Per CogKind atom, the design ships:
//   * a strongly-typed enum class `<Substrate>Opcode` (uint16_t under
//     to keep Bits<> + sentinel-name accessors tractable);
//   * an `OpcodeLatencyEntry<K>` aggregate with quantile-ordered
//     latency triple (Refined-gated p50 ≤ p99 ≤ p999) +
//     throughput_per_sec + sample-count provenance;
//   * an `OpcodeLatencyTable<K>` aggregating a span of entries +
//     calibration staleness grade.
//
// Field-level discipline:
//   * Latency QUANTILE TRIPLE — wrapped in safety::Refined<
//     quantile_ordered, LatencyQuantiles>.  Boundary that constructs
//     from disk-deserialize / federation-import / hostile preset is
//     rejected when ordering is violated (HS14 fixture #2 witnesses).
//   * Sample-count provenance — Tagged<uint16_t, source::Calibrated>.
//     Distinguishes a measured envelope from a default-zeroed entry.
//   * Spanned entries — Tagged<std::span<const Entry>, source::
//     Calibrated>.  The span borrows arena-owned storage; the source
//     tag preserves provenance through OpcodeLatencyTable -> federation
//     snapshot -> Cipher cold tier.
//   * Calibration age — safety::Stale<double>.  The double payload
//     records seconds-since-calibration (a wall-clock metric); the
//     staleness grade τ is a per-cycle counter advanced by Augur's
//     drift detector.  Recalibration trigger: τ exceeds policy
//     threshold (e.g., 100 update cycles without re-measurement).
//
// ── Substrate-to-opcode binding (the load-bearing soundness gate) ───
//
// `opcodes_for<K>::type` (and the alias `opcodes_for_t<K>`) maps each
// schedulable CogKind atom to its concrete opcode enum.  Specialised:
//
//   CogKind::Gpu          → GpuOpcode
//   CogKind::CpuCore      → CpuOpcode
//   CogKind::CpuSocket    → CpuOpcode      (same opcodes apply at L1)
//   CogKind::NicPort      → NicOpcode
//   CogKind::NvSwitch     → SwitchOpcode
//   CogKind::DramChannel  → DramOpcode
//
// The primary template is intentionally undefined.  `HasOpcodeTable<K>`
// surfaces "is K a substrate kind that publishes opcodes?" as a concept
// gate — HS14 fixture #1 witnesses rejection on CogKind::PsuRail.
//
// The concept reuses HasCaps<K> as a structural prerequisite — every
// substrate that publishes opcodes also publishes capability schemas.
// Keeps the two concepts aligned: a Cog either is a schedulable
// substrate (publishes both) or is not (publishes neither).
//
// ── Append-only Universe extension (FOUND-I04, mirrors TargetCaps) ──
//
// **Existing GpuOpcode / NicOpcode / SwitchOpcode / CpuOpcode /
// DramOpcode underlying values are immutable.**  These ordinals feed
// Cipher checkpoint federation row_hash via Tagged<source::Calibrated>
// folding.  Renumbering an opcode (e.g. `Gemm = 1` → `Gemm = 5`) drops
// the cache hit on every snapshot mentioning that opcode.
//
// **New opcodes append only** at the next free ordinal; the in-file pin
// block at the foot of every opcode enum fires a static_assert on
// drift.
//
// SizeBucket / DtypeBucket / TransposeMode / MessageSizeBucket auxiliary
// enums follow the same FOUND-I04 discipline — their underlying values
// participate in the cross-product key that Mimic uses to look up a
// specific entry inside the table.
//
// ── Gates ───────────────────────────────────────────────────────────
//
//   Consumed by:
//     GAPS-188 mimic/CogMimic.h         — Mimic factory reads envelope
//     GAPS-191 cog/FitsCog.h            — row × Cog-budget fit gate
//     GAPS-810 partition optimiser      — placement cost surface
//     GAPS-196 cog/Calibrate.h          — startup measurement writer
//
//   Depends on:
//     cog/CogIdentity.h     — CogKind atoms + IsComputeKind partition
//     cog/TargetCaps.h      — HasCaps<K> structural prerequisite
//     safety/Refined.h      — quantile-ordering invariant
//     safety/Tagged.h       — source::Calibrated provenance
//     safety/Stale.h        — staleness-grade recalibration trigger
//
// References:
//   misc/03_05_2026_networking.md §3.5
//   25_04_2026.md §8 (Stale<> staleness grading)
//   CRUCIBLE.md L2 (Forge / Mimic substrate schemas)

#include <crucible/cog/CogIdentity.h>
#include <crucible/cog/TargetCaps.h>
#include <crucible/safety/Refined.h>
#include <crucible/safety/Stale.h>
#include <crucible/safety/Tagged.h>

#include <cstdint>
#include <meta>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>

namespace crucible::cog {

// ────────────────────────────────────────────────────────────────────
// Auxiliary enums (FOUND-I04 frozen-position bucket ordinals)
// ────────────────────────────────────────────────────────────────────

// SizeBucket — GEMM dimension bucket (M / N / K) that a kernel's
// per-tile cost is parameterised on.  Powers of two from 64 to 4096
// cover the 99th-percentile shape distribution observed across the
// trained-model corpus.  Underlying value IS the bucket size in
// elements (so static_cast<uint16_t>(SizeBucket::S256) == 256), which
// simplifies arithmetic on the underlying value at the call site.
enum class SizeBucket : std::uint16_t {
    None  = 0,
    S64   = 64,
    S128  = 128,
    S256  = 256,
    S512  = 512,
    S1024 = 1024,
    S2048 = 2048,
    S4096 = 4096,
};
inline constexpr std::size_t size_bucket_count =
    std::meta::enumerators_of(^^SizeBucket).size();

[[nodiscard]] constexpr std::string_view
size_bucket_name(SizeBucket B) noexcept {
    switch (B) {
        case SizeBucket::None:  return "None";
        case SizeBucket::S64:   return "S64";
        case SizeBucket::S128:  return "S128";
        case SizeBucket::S256:  return "S256";
        case SizeBucket::S512:  return "S512";
        case SizeBucket::S1024: return "S1024";
        case SizeBucket::S2048: return "S2048";
        case SizeBucket::S4096: return "S4096";
        default: return std::string_view{"<unknown SizeBucket>"};
    }
}

// DtypeBucket — element-type bucket for GEMM / SDPA / fused entries.
// Ordinal-only (no derived numeric meaning, unlike SizeBucket); the
// FOUND-I04 pin block at the foot of the file freezes each ordinal.
enum class DtypeBucket : std::uint8_t {
    None = 0,
    Fp64 = 1,
    Fp32 = 2,
    Tf32 = 3,
    Fp16 = 4,
    Bf16 = 5,
    Fp8  = 6,
    Fp4  = 7,
    Int8 = 8,
};
inline constexpr std::size_t dtype_bucket_count =
    std::meta::enumerators_of(^^DtypeBucket).size();

[[nodiscard]] constexpr std::string_view
dtype_bucket_name(DtypeBucket B) noexcept {
    switch (B) {
        case DtypeBucket::None: return "None";
        case DtypeBucket::Fp64: return "Fp64";
        case DtypeBucket::Fp32: return "Fp32";
        case DtypeBucket::Tf32: return "Tf32";
        case DtypeBucket::Fp16: return "Fp16";
        case DtypeBucket::Bf16: return "Bf16";
        case DtypeBucket::Fp8:  return "Fp8";
        case DtypeBucket::Fp4:  return "Fp4";
        case DtypeBucket::Int8: return "Int8";
        default: return std::string_view{"<unknown DtypeBucket>"};
    }
}

// TransposeMode — matrix-operand transpose bucket for GEMM entries.
// Standard cuBLAS / hipBLAS / oneMKL convention: T = transposed,
// N = non-transposed.  Pair order is (A, B) where the GEMM is
// C = A · B.  Tt → both transposed.  Frozen by FOUND-I04 ordinal.
enum class TransposeMode : std::uint8_t {
    Nn = 0,
    Tn = 1,
    Nt = 2,
    Tt = 3,
};
inline constexpr std::size_t transpose_mode_count =
    std::meta::enumerators_of(^^TransposeMode).size();

[[nodiscard]] constexpr std::string_view
transpose_mode_name(TransposeMode M) noexcept {
    switch (M) {
        case TransposeMode::Nn: return "Nn";
        case TransposeMode::Tn: return "Tn";
        case TransposeMode::Nt: return "Nt";
        case TransposeMode::Tt: return "Tt";
        default: return std::string_view{"<unknown TransposeMode>"};
    }
}

// MessageSizeBucket — collective-op + RDMA / TCP message-size bucket.
// Powers-of-two-ish; the pattern follows the standard message-size
// histograms reported by perftest (ib_send_lat, ib_send_bw) and
// nccl-tests.  Underlying value IS the bucket size in BYTES so a
// static_cast<uint32_t>(MessageSizeBucket::M16K) == 16384 invariant
// holds (consistent with SizeBucket).
enum class MessageSizeBucket : std::uint32_t {
    None  = 0,
    M64B  = 64,
    M1K   = 1024,
    M16K  = 16384,
    M256K = 262144,
    M4M   = 4194304,
    M64M  = 67108864,
};
inline constexpr std::size_t message_size_bucket_count =
    std::meta::enumerators_of(^^MessageSizeBucket).size();

[[nodiscard]] constexpr std::string_view
message_size_bucket_name(MessageSizeBucket B) noexcept {
    switch (B) {
        case MessageSizeBucket::None:  return "None";
        case MessageSizeBucket::M64B:  return "M64B";
        case MessageSizeBucket::M1K:   return "M1K";
        case MessageSizeBucket::M16K:  return "M16K";
        case MessageSizeBucket::M256K: return "M256K";
        case MessageSizeBucket::M4M:   return "M4M";
        case MessageSizeBucket::M64M:  return "M64M";
        default: return std::string_view{"<unknown MessageSizeBucket>"};
    }
}

// ────────────────────────────────────────────────────────────────────
// Per-substrate opcode catalogs (FOUND-I04 frozen-position ordinals)
// ────────────────────────────────────────────────────────────────────

// GpuOpcode — GPU compute / memory / launch opcode catalog.  Covers
// the load-bearing kernels Mimic's MAP-Elites search emits across
// (SizeBucket × DtypeBucket × TransposeMode) cross-product, plus the
// launch-overhead atoms (kernel launch, doorbell ring, completion
// query) that drive shadow-handle dispatch budget.
enum class GpuOpcode : std::uint16_t {
    GemmPlain       = 0,   // dense GEMM C = A · B, parameterised by
                           // (M, N, K) ∈ SizeBucket × dtype × tt mode
    GemmFused       = 1,   // GEMM with epilogue (bias / activation)
    Sdpa            = 2,   // scaled-dot-product attention
    Conv2D          = 3,   // dense 2D convolution
    AllReduceRing   = 4,   // ring all-reduce per N × M-bucket
    AllReduceTree   = 5,   // tree all-reduce per N × M-bucket
    AllGather       = 6,   // all-gather per N × M-bucket
    NvlinkP2pRead   = 7,   // peer-to-peer NVLink read
    NvlinkP2pWrite  = 8,   // peer-to-peer NVLink write
    PciePeer        = 9,   // PCIe peer-DMA across-root-complex
    KernelLaunch    = 10,  // cudaLaunchKernel / cuLaunchKernelEx
    DoorbellRing    = 11,  // GPU doorbell ring (Hopper+ TMA path)
    EventQuery      = 12,  // cudaEventQuery / cuEventQuery (poll)
};
inline constexpr std::size_t gpu_opcode_count =
    std::meta::enumerators_of(^^GpuOpcode).size();

// NicOpcode — NIC / RDMA / TCP / AF_XDP opcode catalog.  Drives the
// network-kernel author's per-message-size envelope (RDMA WRITE at
// 64B vs 4MB has 100× latency / 1000× bandwidth divergence; the
// table records both endpoints and the partition optimiser
// interpolates the surface in between).
enum class NicOpcode : std::uint16_t {
    RdmaWrite        = 0,
    RdmaSend         = 1,
    RdmaRead         = 2,
    CompletionPoll   = 3,
    QpCreate         = 4,
    QpDestroy        = 5,
    MrRegister       = 6,
    MrDeregister     = 7,
    DoorbellRing     = 8,
    TcpSend          = 9,
    TcpRecv          = 10,
    AfXdpEnqueue     = 11,
    AfXdpDequeue     = 12,
    GpuDirectWrite   = 13,
    GpuDirectRead    = 14,
};
inline constexpr std::size_t nic_opcode_count =
    std::meta::enumerators_of(^^NicOpcode).size();

// SwitchOpcode — switch / fabric opcode catalog.  Scheduling decisions
// at L2 (Server) and above depend on per-port forwarding latency and
// SHARP-eligibility.  Smaller catalog by design — the switch is a
// pass-through fabric, not a compute substrate.
enum class SwitchOpcode : std::uint16_t {
    PortForward        = 0,
    AclMatch           = 1,
    SharpReduce        = 2,
    MulticastReplicate = 3,
};
inline constexpr std::size_t switch_opcode_count =
    std::meta::enumerators_of(^^SwitchOpcode).size();

// CpuOpcode — host-CPU opcode catalog.  Covers the per-core SIMD
// micro-kernels Mimic-CPU emits (oracle backend, MIMIC.md §41), the
// scheduling primitives the Keeper depends on (ContextSwitch,
// AtomicCas), and the OS-mediated atoms (Syscall) that the partition
// optimiser excludes from compute-bound regions.
enum class CpuOpcode : std::uint16_t {
    Memcpy         = 0,
    Vfma           = 1,
    AvxLoad        = 2,
    AvxStore       = 3,
    ContextSwitch  = 4,
    AtomicCas      = 5,
    MutexLock      = 6,
    MutexUnlock    = 7,
    FutexWait      = 8,
    Syscall        = 9,
};
inline constexpr std::size_t cpu_opcode_count =
    std::meta::enumerators_of(^^CpuOpcode).size();

// DramOpcode — DRAM-channel opcode catalog.  L0 atomic latency for
// the per-row / per-bank operations the memory subsystem performs.
// Calibrated by the DRAM controller benchmarks at startup; consumed
// by the partition optimiser when memory-bandwidth-bound regions
// drive the placement decision.
enum class DramOpcode : std::uint16_t {
    ChannelRead   = 0,
    ChannelWrite  = 1,
    RowActivate   = 2,
    BankRefresh   = 3,
    Precharge     = 4,
};
inline constexpr std::size_t dram_opcode_count =
    std::meta::enumerators_of(^^DramOpcode).size();

// ── Diagnostic name accessors (constexpr for runtime smoke discipline)

[[nodiscard]] constexpr std::string_view
gpu_opcode_name(GpuOpcode O) noexcept {
    switch (O) {
        case GpuOpcode::GemmPlain:      return "GemmPlain";
        case GpuOpcode::GemmFused:      return "GemmFused";
        case GpuOpcode::Sdpa:           return "Sdpa";
        case GpuOpcode::Conv2D:         return "Conv2D";
        case GpuOpcode::AllReduceRing:  return "AllReduceRing";
        case GpuOpcode::AllReduceTree:  return "AllReduceTree";
        case GpuOpcode::AllGather:      return "AllGather";
        case GpuOpcode::NvlinkP2pRead:  return "NvlinkP2pRead";
        case GpuOpcode::NvlinkP2pWrite: return "NvlinkP2pWrite";
        case GpuOpcode::PciePeer:       return "PciePeer";
        case GpuOpcode::KernelLaunch:   return "KernelLaunch";
        case GpuOpcode::DoorbellRing:   return "DoorbellRing";
        case GpuOpcode::EventQuery:     return "EventQuery";
        default: return std::string_view{"<unknown GpuOpcode>"};
    }
}

[[nodiscard]] constexpr std::string_view
nic_opcode_name(NicOpcode O) noexcept {
    switch (O) {
        case NicOpcode::RdmaWrite:       return "RdmaWrite";
        case NicOpcode::RdmaSend:        return "RdmaSend";
        case NicOpcode::RdmaRead:        return "RdmaRead";
        case NicOpcode::CompletionPoll:  return "CompletionPoll";
        case NicOpcode::QpCreate:        return "QpCreate";
        case NicOpcode::QpDestroy:       return "QpDestroy";
        case NicOpcode::MrRegister:      return "MrRegister";
        case NicOpcode::MrDeregister:    return "MrDeregister";
        case NicOpcode::DoorbellRing:    return "DoorbellRing";
        case NicOpcode::TcpSend:         return "TcpSend";
        case NicOpcode::TcpRecv:         return "TcpRecv";
        case NicOpcode::AfXdpEnqueue:    return "AfXdpEnqueue";
        case NicOpcode::AfXdpDequeue:    return "AfXdpDequeue";
        case NicOpcode::GpuDirectWrite:  return "GpuDirectWrite";
        case NicOpcode::GpuDirectRead:   return "GpuDirectRead";
        default: return std::string_view{"<unknown NicOpcode>"};
    }
}

[[nodiscard]] constexpr std::string_view
switch_opcode_name(SwitchOpcode O) noexcept {
    switch (O) {
        case SwitchOpcode::PortForward:        return "PortForward";
        case SwitchOpcode::AclMatch:           return "AclMatch";
        case SwitchOpcode::SharpReduce:        return "SharpReduce";
        case SwitchOpcode::MulticastReplicate: return "MulticastReplicate";
        default: return std::string_view{"<unknown SwitchOpcode>"};
    }
}

[[nodiscard]] constexpr std::string_view
cpu_opcode_name(CpuOpcode O) noexcept {
    switch (O) {
        case CpuOpcode::Memcpy:        return "Memcpy";
        case CpuOpcode::Vfma:          return "Vfma";
        case CpuOpcode::AvxLoad:       return "AvxLoad";
        case CpuOpcode::AvxStore:      return "AvxStore";
        case CpuOpcode::ContextSwitch: return "ContextSwitch";
        case CpuOpcode::AtomicCas:     return "AtomicCas";
        case CpuOpcode::MutexLock:     return "MutexLock";
        case CpuOpcode::MutexUnlock:   return "MutexUnlock";
        case CpuOpcode::FutexWait:     return "FutexWait";
        case CpuOpcode::Syscall:       return "Syscall";
        default: return std::string_view{"<unknown CpuOpcode>"};
    }
}

[[nodiscard]] constexpr std::string_view
dram_opcode_name(DramOpcode O) noexcept {
    switch (O) {
        case DramOpcode::ChannelRead:  return "ChannelRead";
        case DramOpcode::ChannelWrite: return "ChannelWrite";
        case DramOpcode::RowActivate:  return "RowActivate";
        case DramOpcode::BankRefresh:  return "BankRefresh";
        case DramOpcode::Precharge:    return "Precharge";
        default: return std::string_view{"<unknown DramOpcode>"};
    }
}

// ────────────────────────────────────────────────────────────────────
// Quantile-ordering invariant (Refined predicate)
// ────────────────────────────────────────────────────────────────────

// LatencyQuantiles — per-opcode latency triple in nanoseconds.
// Aggregate type so it remains trivially copyable and disk-loadable;
// the quantile-ordering invariant is enforced one level up via the
// safety::Refined<quantile_ordered, LatencyQuantiles> alias below.
struct LatencyQuantiles {
    std::uint32_t p50_ns  = 0;
    std::uint32_t p99_ns  = 0;
    std::uint32_t p999_ns = 0;

    [[nodiscard]] friend constexpr bool
    operator==(const LatencyQuantiles&,
               const LatencyQuantiles&) noexcept = default;
};

static_assert(sizeof(LatencyQuantiles) == 3 * sizeof(std::uint32_t),
    "LatencyQuantiles drift — packing assumption broken; downstream "
    "Cipher snapshot byte layout is wrong.");
static_assert(std::is_trivially_copyable_v<LatencyQuantiles>);
static_assert(std::is_standard_layout_v<LatencyQuantiles>);

// Predicate: p50 ≤ p99 ≤ p999.  Quantile-monotonicity is a structural
// invariant of any well-formed measurement — a triple that violates
// it is either disk-corrupted, federation-imported under a different
// histogram convention, or the calibrator silently mislabeled the
// fields.  Constructing a Refined<quantile_ordered, _> from such a
// triple fires the precondition contract and rejects at construction.
inline constexpr auto quantile_ordered =
    [](const LatencyQuantiles& q) constexpr noexcept {
        return q.p50_ns <= q.p99_ns && q.p99_ns <= q.p999_ns;
    };

using OrderedLatencyQuantiles =
    safety::Refined<quantile_ordered, LatencyQuantiles>;

static_assert(sizeof(OrderedLatencyQuantiles) == sizeof(LatencyQuantiles),
    "OrderedLatencyQuantiles must EBO-collapse to sizeof("
    "LatencyQuantiles) — Refined<lambda, T> with empty BoolLattice "
    "lattice element_type is regime-1.");

// ────────────────────────────────────────────────────────────────────
// opcodes_for<CogKind> — kind-to-opcode-enum binding metafunction
// ────────────────────────────────────────────────────────────────────

// Primary template — INTENTIONALLY UNDEFINED.  Reaching here means the
// caller asked `opcodes_for_t<K>` for a CogKind that has no opcode
// catalog (e.g. PsuRail, BmcSensor — non-schedulable Cogs).  Compile
// error names the concept gate failure.
template <CogKind K>
struct opcodes_for;

template <> struct opcodes_for<CogKind::Gpu>          { using type = GpuOpcode;    };
template <> struct opcodes_for<CogKind::CpuCore>      { using type = CpuOpcode;    };
template <> struct opcodes_for<CogKind::CpuSocket>    { using type = CpuOpcode;    };
template <> struct opcodes_for<CogKind::NicPort>      { using type = NicOpcode;    };
template <> struct opcodes_for<CogKind::NvSwitch>     { using type = SwitchOpcode; };
template <> struct opcodes_for<CogKind::DramChannel>  { using type = DramOpcode;   };

template <CogKind K>
using opcodes_for_t = typename opcodes_for<K>::type;

// HasOpcodeTable<K> — concept gate.  Constrained templates downstream
// (Mimic factories, partition optimiser) refuse non-substrate kinds
// at substitution time per HS14.  The HasCaps<K> conjunct binds the
// two concepts: a Cog publishes opcodes IFF it publishes capability
// schemas, by structural alignment.
template <CogKind K>
concept HasOpcodeTable = HasCaps<K>
                     && requires { typename opcodes_for<K>::type; };

// ────────────────────────────────────────────────────────────────────
// OpcodeLatencyEntry / OpcodeLatencyTable
// ────────────────────────────────────────────────────────────────────

// Per-opcode calibrated entry.  One row of the per-Cog latency table.
//
// Layout:
//   * `opcode` — the opcode-enum atom this row records.
//   * `size_bucket` / `dtype_bucket` / `transpose_mode` /
//     `message_size_bucket` — the cross-product key.  Not all opcodes
//     use every dimension (an `EventQuery` row has all four bucket
//     fields = None; a `GemmPlain` row uses size + dtype + transpose;
//     an `AllReduceRing` row uses size + message-size).  Unused
//     dimensions are explicitly None.
//   * `latency_cycles` — calibrated p50 latency in CPU cycles (used
//     by the cycle-budget cost model where wall-clock ns is too
//     coarse for shadow-handle dispatch math).
//   * `latency` — wall-clock quantile triple (p50 / p99 / p999) in
//     nanoseconds, Refined-ordered per the quantile_ordered predicate.
//   * `throughput_per_sec` — calibrated throughput envelope (ops/sec
//     for compute opcodes, bytes/sec for transfer opcodes).
//   * `sample_count` — number of measurements that produced this
//     entry.  Tagged source::Calibrated to distinguish from default-
//     zeroed entries (sample_count == 0 → uncalibrated, downstream
//     consumer falls back to vendor-spec).
template <CogKind K>
    requires HasOpcodeTable<K>
struct OpcodeLatencyEntry {
    using OpcodeId = opcodes_for_t<K>;

    OpcodeId          opcode{};
    SizeBucket        size_bucket        = SizeBucket::None;
    DtypeBucket       dtype_bucket       = DtypeBucket::None;
    TransposeMode     transpose_mode     = TransposeMode::Nn;
    MessageSizeBucket message_size_bucket = MessageSizeBucket::None;

    std::uint32_t latency_cycles = 0;
    OrderedLatencyQuantiles latency{LatencyQuantiles{}};
    double throughput_per_sec = 0.0;

    safety::Tagged<std::uint16_t, safety::source::Calibrated>
        sample_count{std::uint16_t{0}};
};

// Per-Cog opcode latency table.  Aggregates a span of calibrated
// entries plus a staleness grade tracking how long since the last
// re-calibration.
//
// Storage strategy:
//   * `entries` is a non-owning std::span borrowed from arena-owned
//     storage.  The Tagged source::Calibrated tag distinguishes a
//     measured table from a default / uninitialised one (default
//     constructor produces an empty span, which downstream consumers
//     interpret as "fall back to vendor-spec defaults").
//   * `calibration_age_seconds` is a Stale<double> — payload records
//     wall-clock seconds since the calibration measurement; staleness
//     grade τ tracks update-cycle staleness.  Augur's drift detector
//     bumps τ via Stale::advance_by; recalibration is triggered when
//     τ exceeds the policy threshold.
//
// Query helpers operate on the borrowed span — O(N) linear scan over
// a typical table size of 50-200 entries.  Kept linear-not-hashed
// because the call frequency is ~once-per-iteration and N is small;
// adding a hash table would cost more in cache-line overhead than the
// linear scan saves.
template <CogKind K>
    requires HasOpcodeTable<K>
struct OpcodeLatencyTable {
    using Entry    = OpcodeLatencyEntry<K>;
    using OpcodeId = opcodes_for_t<K>;

    safety::Tagged<std::span<const Entry>, safety::source::Calibrated>
        entries{std::span<const Entry>{}};

    safety::Stale<double> calibration_age_seconds{};

    // ── Query helpers ──────────────────────────────────────────────

    // Find the first entry whose opcode matches.  Returns nullopt if
    // no row publishes that opcode (typical for an uncalibrated table
    // or when an opcode applies to a sub-product key that isn't in
    // this row).  Linear scan; O(N) per call over a typical 50-200
    // row table.
    [[nodiscard]] constexpr std::optional<Entry>
    lookup_by_opcode(OpcodeId target) const noexcept {
        const auto& span_view = entries.value();
        for (const Entry& e : span_view) {
            if (e.opcode == target) return e;
        }
        return std::nullopt;
    }

    // Find the entry matching (opcode, size, dtype) — full-key lookup
    // for the GEMM-style cross-product entries.  transpose_mode +
    // message_size_bucket default to Nn / None for the simpler
    // catalog rows that don't use them.
    [[nodiscard]] constexpr std::optional<Entry>
    latency_for_size_bucket(OpcodeId       target_opcode,
                            SizeBucket     target_size,
                            DtypeBucket    target_dtype,
                            TransposeMode  target_transpose = TransposeMode::Nn,
                            MessageSizeBucket target_msgsz = MessageSizeBucket::None) const noexcept
    {
        const auto& span_view = entries.value();
        for (const Entry& e : span_view) {
            if (e.opcode              == target_opcode  &&
                e.size_bucket         == target_size    &&
                e.dtype_bucket        == target_dtype   &&
                e.transpose_mode      == target_transpose &&
                e.message_size_bucket == target_msgsz) return e;
        }
        return std::nullopt;
    }

    // Aggregate throughput envelope across all entries matching the
    // given opcode.  Used by the partition optimiser's coarse
    // budget-fitting pass — sum across (size × dtype × transpose)
    // expansions of one opcode to estimate total available throughput
    // at this Cog.
    [[nodiscard]] constexpr double
    throughput_envelope(OpcodeId target_opcode) const noexcept {
        const auto& span_view = entries.value();
        double sum = 0.0;
        for (const Entry& e : span_view) {
            if (e.opcode == target_opcode) sum += e.throughput_per_sec;
        }
        return sum;
    }

    // Number of entries — convenience accessor over the borrowed span.
    [[nodiscard]] constexpr std::size_t size() const noexcept {
        return entries.value().size();
    }

    // Empty-table predicate.  Default-constructed tables AND tables
    // whose calibration produced no rows are both "empty" — the
    // downstream consumer falls back to vendor-spec defaults.
    [[nodiscard]] constexpr bool empty() const noexcept {
        return entries.value().empty();
    }
};

// ────────────────────────────────────────────────────────────────────
// Self-test block — name coverage + FOUND-I04 frozen-position pins
// ────────────────────────────────────────────────────────────────────

namespace detail::opcode_latency_self_test {

// ── Sizeof sanity (regime-1 EBO collapse) ──────────────────────────
//
// Refined<quantile_ordered, LatencyQuantiles> must collapse to
// sizeof(LatencyQuantiles) — BoolLattice<lambda> has empty
// element_type.
static_assert(sizeof(OrderedLatencyQuantiles) == sizeof(LatencyQuantiles));

// Tagged<span<const T>, source::Calibrated> must collapse to
// sizeof(span) — TrustLattice<source::Calibrated> has empty
// element_type.  The sample_count Tagged collapses similarly.
static_assert(sizeof(safety::Tagged<std::uint16_t, safety::source::Calibrated>)
              == sizeof(std::uint16_t));
static_assert(sizeof(safety::Tagged<std::span<const int>, safety::source::Calibrated>)
              == sizeof(std::span<const int>));

// Stale<double> sizeof: per Stale.h layout discipline ≤ 16 bytes
// (8B value + 8B staleness grade, no padding for double).
static_assert(sizeof(safety::Stale<double>) <= 16);

// ── Reflection-driven name coverage (FOUND-I04 discipline) ─────────
//
// Every enumerator MUST have a non-sentinel name from the corresponding
// *_name accessor.  Adding a new enumerator without updating the switch
// fires the matching assertion at header-inclusion time.

[[nodiscard]] consteval bool every_size_bucket_has_name() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^SizeBucket));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (size_bucket_name([:en:]) ==
            std::string_view{"<unknown SizeBucket>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_size_bucket_has_name(),
    "size_bucket_name() switch missing arm for at least one SizeBucket "
    "atom.");

[[nodiscard]] consteval bool every_dtype_bucket_has_name() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^DtypeBucket));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (dtype_bucket_name([:en:]) ==
            std::string_view{"<unknown DtypeBucket>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_dtype_bucket_has_name());

[[nodiscard]] consteval bool every_transpose_mode_has_name() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^TransposeMode));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (transpose_mode_name([:en:]) ==
            std::string_view{"<unknown TransposeMode>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_transpose_mode_has_name());

[[nodiscard]] consteval bool every_message_size_bucket_has_name() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^MessageSizeBucket));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (message_size_bucket_name([:en:]) ==
            std::string_view{"<unknown MessageSizeBucket>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_message_size_bucket_has_name());

[[nodiscard]] consteval bool every_gpu_opcode_has_name() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^GpuOpcode));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (gpu_opcode_name([:en:]) ==
            std::string_view{"<unknown GpuOpcode>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_gpu_opcode_has_name());

[[nodiscard]] consteval bool every_nic_opcode_has_name() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^NicOpcode));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (nic_opcode_name([:en:]) ==
            std::string_view{"<unknown NicOpcode>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_nic_opcode_has_name());

[[nodiscard]] consteval bool every_switch_opcode_has_name() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^SwitchOpcode));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (switch_opcode_name([:en:]) ==
            std::string_view{"<unknown SwitchOpcode>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_switch_opcode_has_name());

[[nodiscard]] consteval bool every_cpu_opcode_has_name() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^CpuOpcode));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (cpu_opcode_name([:en:]) ==
            std::string_view{"<unknown CpuOpcode>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_cpu_opcode_has_name());

[[nodiscard]] consteval bool every_dram_opcode_has_name() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^DramOpcode));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (dram_opcode_name([:en:]) ==
            std::string_view{"<unknown DramOpcode>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_dram_opcode_has_name());

// ── FOUND-I04 frozen-position pins ─────────────────────────────────

// SizeBucket (underlying value IS the bucket size in elements)
static_assert(static_cast<std::uint16_t>(SizeBucket::None)  == 0,
    "SizeBucket::None drifted — federation row_hash invalidated.  "
    "Pin lives in cog/OpcodeLatencyTable.h.");
static_assert(static_cast<std::uint16_t>(SizeBucket::S64)   == 64);
static_assert(static_cast<std::uint16_t>(SizeBucket::S128)  == 128);
static_assert(static_cast<std::uint16_t>(SizeBucket::S256)  == 256);
static_assert(static_cast<std::uint16_t>(SizeBucket::S512)  == 512);
static_assert(static_cast<std::uint16_t>(SizeBucket::S1024) == 1024);
static_assert(static_cast<std::uint16_t>(SizeBucket::S2048) == 2048);
static_assert(static_cast<std::uint16_t>(SizeBucket::S4096) == 4096);

// DtypeBucket
static_assert(static_cast<std::uint8_t>(DtypeBucket::None) == 0);
static_assert(static_cast<std::uint8_t>(DtypeBucket::Fp64) == 1);
static_assert(static_cast<std::uint8_t>(DtypeBucket::Fp32) == 2);
static_assert(static_cast<std::uint8_t>(DtypeBucket::Tf32) == 3);
static_assert(static_cast<std::uint8_t>(DtypeBucket::Fp16) == 4);
static_assert(static_cast<std::uint8_t>(DtypeBucket::Bf16) == 5);
static_assert(static_cast<std::uint8_t>(DtypeBucket::Fp8)  == 6);
static_assert(static_cast<std::uint8_t>(DtypeBucket::Fp4)  == 7);
static_assert(static_cast<std::uint8_t>(DtypeBucket::Int8) == 8);

// TransposeMode
static_assert(static_cast<std::uint8_t>(TransposeMode::Nn) == 0);
static_assert(static_cast<std::uint8_t>(TransposeMode::Tn) == 1);
static_assert(static_cast<std::uint8_t>(TransposeMode::Nt) == 2);
static_assert(static_cast<std::uint8_t>(TransposeMode::Tt) == 3);

// MessageSizeBucket (underlying value IS the bucket size in BYTES)
static_assert(static_cast<std::uint32_t>(MessageSizeBucket::None)  == 0);
static_assert(static_cast<std::uint32_t>(MessageSizeBucket::M64B)  == 64);
static_assert(static_cast<std::uint32_t>(MessageSizeBucket::M1K)   == 1024);
static_assert(static_cast<std::uint32_t>(MessageSizeBucket::M16K)  == 16384);
static_assert(static_cast<std::uint32_t>(MessageSizeBucket::M256K) == 262144);
static_assert(static_cast<std::uint32_t>(MessageSizeBucket::M4M)   == 4194304);
static_assert(static_cast<std::uint32_t>(MessageSizeBucket::M64M)  == 67108864);

// GpuOpcode — ordinal pins
static_assert(static_cast<std::uint16_t>(GpuOpcode::GemmPlain)      == 0);
static_assert(static_cast<std::uint16_t>(GpuOpcode::GemmFused)      == 1);
static_assert(static_cast<std::uint16_t>(GpuOpcode::Sdpa)           == 2);
static_assert(static_cast<std::uint16_t>(GpuOpcode::Conv2D)         == 3);
static_assert(static_cast<std::uint16_t>(GpuOpcode::AllReduceRing)  == 4);
static_assert(static_cast<std::uint16_t>(GpuOpcode::AllReduceTree)  == 5);
static_assert(static_cast<std::uint16_t>(GpuOpcode::AllGather)      == 6);
static_assert(static_cast<std::uint16_t>(GpuOpcode::NvlinkP2pRead)  == 7);
static_assert(static_cast<std::uint16_t>(GpuOpcode::NvlinkP2pWrite) == 8);
static_assert(static_cast<std::uint16_t>(GpuOpcode::PciePeer)       == 9);
static_assert(static_cast<std::uint16_t>(GpuOpcode::KernelLaunch)   == 10);
static_assert(static_cast<std::uint16_t>(GpuOpcode::DoorbellRing)   == 11);
static_assert(static_cast<std::uint16_t>(GpuOpcode::EventQuery)     == 12);

// NicOpcode — ordinal pins
static_assert(static_cast<std::uint16_t>(NicOpcode::RdmaWrite)      == 0);
static_assert(static_cast<std::uint16_t>(NicOpcode::RdmaSend)       == 1);
static_assert(static_cast<std::uint16_t>(NicOpcode::RdmaRead)       == 2);
static_assert(static_cast<std::uint16_t>(NicOpcode::CompletionPoll) == 3);
static_assert(static_cast<std::uint16_t>(NicOpcode::QpCreate)       == 4);
static_assert(static_cast<std::uint16_t>(NicOpcode::QpDestroy)      == 5);
static_assert(static_cast<std::uint16_t>(NicOpcode::MrRegister)     == 6);
static_assert(static_cast<std::uint16_t>(NicOpcode::MrDeregister)   == 7);
static_assert(static_cast<std::uint16_t>(NicOpcode::DoorbellRing)   == 8);
static_assert(static_cast<std::uint16_t>(NicOpcode::TcpSend)        == 9);
static_assert(static_cast<std::uint16_t>(NicOpcode::TcpRecv)        == 10);
static_assert(static_cast<std::uint16_t>(NicOpcode::AfXdpEnqueue)   == 11);
static_assert(static_cast<std::uint16_t>(NicOpcode::AfXdpDequeue)   == 12);
static_assert(static_cast<std::uint16_t>(NicOpcode::GpuDirectWrite) == 13);
static_assert(static_cast<std::uint16_t>(NicOpcode::GpuDirectRead)  == 14);

// SwitchOpcode
static_assert(static_cast<std::uint16_t>(SwitchOpcode::PortForward)        == 0);
static_assert(static_cast<std::uint16_t>(SwitchOpcode::AclMatch)           == 1);
static_assert(static_cast<std::uint16_t>(SwitchOpcode::SharpReduce)        == 2);
static_assert(static_cast<std::uint16_t>(SwitchOpcode::MulticastReplicate) == 3);

// CpuOpcode
static_assert(static_cast<std::uint16_t>(CpuOpcode::Memcpy)        == 0);
static_assert(static_cast<std::uint16_t>(CpuOpcode::Vfma)          == 1);
static_assert(static_cast<std::uint16_t>(CpuOpcode::AvxLoad)       == 2);
static_assert(static_cast<std::uint16_t>(CpuOpcode::AvxStore)      == 3);
static_assert(static_cast<std::uint16_t>(CpuOpcode::ContextSwitch) == 4);
static_assert(static_cast<std::uint16_t>(CpuOpcode::AtomicCas)     == 5);
static_assert(static_cast<std::uint16_t>(CpuOpcode::MutexLock)     == 6);
static_assert(static_cast<std::uint16_t>(CpuOpcode::MutexUnlock)   == 7);
static_assert(static_cast<std::uint16_t>(CpuOpcode::FutexWait)     == 8);
static_assert(static_cast<std::uint16_t>(CpuOpcode::Syscall)       == 9);

// DramOpcode
static_assert(static_cast<std::uint16_t>(DramOpcode::ChannelRead)  == 0);
static_assert(static_cast<std::uint16_t>(DramOpcode::ChannelWrite) == 1);
static_assert(static_cast<std::uint16_t>(DramOpcode::RowActivate)  == 2);
static_assert(static_cast<std::uint16_t>(DramOpcode::BankRefresh)  == 3);
static_assert(static_cast<std::uint16_t>(DramOpcode::Precharge)    == 4);

// Underlying-type pins — ABI-breaking widen would silently re-shape
// every OpcodeLatencyEntry that embeds the enum by value.
static_assert(std::is_same_v<std::underlying_type_t<SizeBucket>,        std::uint16_t>);
static_assert(std::is_same_v<std::underlying_type_t<DtypeBucket>,       std::uint8_t>);
static_assert(std::is_same_v<std::underlying_type_t<TransposeMode>,     std::uint8_t>);
static_assert(std::is_same_v<std::underlying_type_t<MessageSizeBucket>, std::uint32_t>);
static_assert(std::is_same_v<std::underlying_type_t<GpuOpcode>,         std::uint16_t>);
static_assert(std::is_same_v<std::underlying_type_t<NicOpcode>,         std::uint16_t>);
static_assert(std::is_same_v<std::underlying_type_t<SwitchOpcode>,      std::uint16_t>);
static_assert(std::is_same_v<std::underlying_type_t<CpuOpcode>,         std::uint16_t>);
static_assert(std::is_same_v<std::underlying_type_t<DramOpcode>,        std::uint16_t>);

// ── opcodes_for binding sanity ─────────────────────────────────────
//
// Every specialised CogKind has the matching opcode-enum bound.
// Cross-binding mismatch (opcodes_for<Gpu> resolving to NicOpcode,
// opcodes_for<NicPort> resolving to GpuOpcode) would silently route
// a GPU opcode through the network catalog — instant catastrophic
// confusion.  Pin the bindings here.
static_assert(std::is_same_v<opcodes_for_t<CogKind::Gpu>,         GpuOpcode>);
static_assert(std::is_same_v<opcodes_for_t<CogKind::CpuCore>,     CpuOpcode>);
static_assert(std::is_same_v<opcodes_for_t<CogKind::CpuSocket>,   CpuOpcode>);
static_assert(std::is_same_v<opcodes_for_t<CogKind::NicPort>,     NicOpcode>);
static_assert(std::is_same_v<opcodes_for_t<CogKind::NvSwitch>,    SwitchOpcode>);
static_assert(std::is_same_v<opcodes_for_t<CogKind::DramChannel>, DramOpcode>);

// HasOpcodeTable<K> — substrate kinds satisfy; non-substrate kinds
// (PsuRail, BmcSensor, the L2..L7 aggregates) do NOT.  HS14 fixture #1
// in test/cog_neg/ witnesses the rejection.
static_assert(HasOpcodeTable<CogKind::Gpu>);
static_assert(HasOpcodeTable<CogKind::CpuCore>);
static_assert(HasOpcodeTable<CogKind::CpuSocket>);
static_assert(HasOpcodeTable<CogKind::NicPort>);
static_assert(HasOpcodeTable<CogKind::NvSwitch>);
static_assert(HasOpcodeTable<CogKind::DramChannel>);
static_assert(!HasOpcodeTable<CogKind::PsuRail>);
static_assert(!HasOpcodeTable<CogKind::BmcSensor>);
static_assert(!HasOpcodeTable<CogKind::Datacenter>);

// ── Layout sanity for downstream ABI ───────────────────────────────
//
// Standard-layout requirement — tables may be serialised to disk
// (Cipher) or shipped over the wire (federation snapshot).  The
// templated structs are aggregates of trivially-copyable wrappers,
// satisfying standard-layout trivially.
static_assert(std::is_standard_layout_v<LatencyQuantiles>);
static_assert(std::is_standard_layout_v<OpcodeLatencyEntry<CogKind::Gpu>>);
static_assert(std::is_standard_layout_v<OpcodeLatencyEntry<CogKind::NicPort>>);

}  // namespace detail::opcode_latency_self_test

}  // namespace crucible::cog
