#pragma once

// ── crucible::cog::TargetCaps — per-Cog capability schemas ──────────
//
// GAPS-186.  Per misc/03_05_2026_networking.md §3.4 and CRUCIBLE.md L2.
// Each schedulable substrate Cog (GPU die, CPU socket, NIC port, NVSwitch
// port, DRAM channel) publishes a typed capability schema that captures
// the load-bearing structural facts a downstream consumer needs:
//
//   * the Forge/Mimic kernel-author needs sm_count / regs_per_sm /
//     smem_per_sm / l2_bytes / hbm_bytes / hbm_bandwidth to size tiles;
//   * the network-kernel author needs line_rate / mtu / max_qp_count /
//     calibrated effective_bandwidth to pick CC + pacing;
//   * the per-Cog Mimic factory (GAPS-188) needs feature flags (TMA,
//     ClusterLaunch, FP8, GPUDirect_RDMA, AVX-512, AMX, ...) to gate
//     ISA-specific code paths;
//   * the row-typed budget gate (GAPS-191 cog/FitsCog.h) needs typed
//     fields it can read by ResourceKind axis to compare against the
//     declared row consumption from effects::Concurrent.
//
// ── Composition ─────────────────────────────────────────────────────
//
// Field-level discipline:
//   * Hard structural invariants — wrapped in safety::Refined.  Example:
//     `warp_size` is power-of-two ∧ ≤ 128 on every shipped GPU; rejection
//     at construction time eliminates silent miscount in occupancy math.
//     `sm_count` / `core_count` / `port_count` are deliberately NOT
//     Refined<positive>: a default-constructed schema (pre-discovery) has
//     value 0, which is a legitimate "uninitialized" sentinel; the
//     positivity invariant is a discovery-time post-condition that lives
//     downstream in GAPS-196 Calibrate.h, not at the field type level.
//   * Vendor-supplied datasheet values — wrapped in
//     safety::Tagged<T, source::Vendor>.  Pins the provenance so an
//     accidental swap with a calibrated-then-spoofed value is caught
//     at the Tagged-source mismatch site.
//   * Calibrated runtime values (effective_bandwidth_gbps,
//     bdp_ceiling_gbps, calibrated peak TFLOPS) — wrapped in
//     safety::Tagged<T, source::Calibrated>.  Distinct phantom tag from
//     source::Vendor so a downstream consumer that demands "this MUST be
//     measured, not spec sheet" can demand the correct provenance via
//     the type system.
//   * Boolean feature flags — collected into safety::Bits<FeatureEnum>.
//     One byte (or two/four for wide feature sets), TypeSafe (mismatch
//     between GpuFeature and NicFeature flags is a compile error per
//     Bits<E1> vs Bits<E2> different-instantiation rule), zero-cost
//     popcount/test/set.
//
// ── Substrate-to-schema binding (the load-bearing soundness gate) ───
//
// `caps_for<K>::type` (and the alias `caps_for_t<K>`) maps each
// schedulable CogKind atom to its concrete schema.  Specialised for:
//
//   CogKind::Gpu          → GpuTargetCaps           (compute substrate)
//   CogKind::CpuCore      → CpuCoreTargetCaps       (compute substrate)
//   CogKind::CpuSocket    → CpuSocketTargetCaps     (host aggregate)
//   CogKind::NicPort      → NicPortTargetCaps       (network substrate)
//   CogKind::NvSwitch     → NvSwitchTargetCaps      (fabric substrate)
//   CogKind::DramChannel  → DramChannelTargetCaps   (memory substrate)
//
// The primary template `caps_for<K>` is intentionally undefined — every
// query goes through a specialisation.  The companion concept
// `HasCaps<K>` exposes "is K a substrate kind that publishes caps?" as a
// constraint a constrained template can refuse on (HS14 fixture #1
// witnesses this rejection on CogKind::PsuRail — a power-rail Cog, no
// schedulable workload, no TargetCaps).
//
// Adding a future substrate (e.g. CogKind::OpticalTransceiver gets a
// dedicated FabricLinkTargetCaps when wavelength-aware wave-share
// scheduling lands) is a single-line addition: append the spec template
// and add the caps_for<K> specialisation.  The `HasCaps<K>` consumers
// pick up the new substrate without modification.
//
// ── Append-only Universe extension (FOUND-I04, mirrors Capabilities.h)
//
// **Existing GpuFeature / NicFeature / SwitchFeature / CpuFeature /
// DramFeature underlying values are immutable.**  These bit positions
// feed the federation row_hash via Bits<E>::raw() folding through
// every TargetCaps that travels in a Cipher checkpoint.  Renumbering a
// feature flag (e.g., changing `Tma = 0x04` to `Tma = 0x10`) invalidates
// every cached snapshot that mentioned a TargetCaps with that flag set.
//
// **New flags append only** at the next free bit position; the in-file
// pin block at the foot of every feature enum fires a static_assert on
// drift.
//
// ── Gates ───────────────────────────────────────────────────────────
//
//   Consumed by:
//     GAPS-187 cog/OpcodeLatencyTable.h — per-Cog calibrated table
//     GAPS-188 mimic/CogMimic.h         — per-Cog Mimic factory
//     GAPS-191 cog/FitsCog.h            — Row ≤ Cog::TargetCaps gate
//     GAPS-196 cog/Calibrate.h          — startup measurement writer
//     GAPS-149..158 forge catalogs      — kernel author reads tile
//                                          ceilings (sm_count, smem_per_sm)
//
//   Depends on:
//     cog/CogIdentity.h     — CogKind atoms + CogFamily classification
//     safety/Refined.h      — Refined<Pred, T> structural invariants
//     safety/RefinedAlgebra.h — predicate composition (all_of, in_range)
//     safety/Tagged.h       — source::Vendor + source::Calibrated tags
//     safety/Bits.h         — Bits<EnumType> bitfield carrier
//     effects/Resources.h   — ResourceKind atom catalog (GAPS-189)
//
// References:
//   misc/03_05_2026_networking.md §3.4
//   25_04_2026.md §3.3 (Met(X) row machinery)
//   CRUCIBLE.md L2 (Forge / Mimic substrate schemas)

#include <crucible/cog/CogIdentity.h>
#include <crucible/safety/Bits.h>
#include <crucible/safety/Refined.h>
#include <crucible/safety/RefinedAlgebra.h>
#include <crucible/safety/Tagged.h>

#include <cstdint>
#include <meta>
#include <span>
#include <string_view>
#include <type_traits>

namespace crucible::cog {

// ────────────────────────────────────────────────────────────────────
// Auxiliary enums shared across schemas
// ────────────────────────────────────────────────────────────────────

// Link-layer protocol exposed by a NicPort.  Frozen by underlying value
// per FOUND-I04 — federation-shipped TargetCaps embed this enum's value
// in row_hash via the Tagged<LinkLayer, source::Vendor> field below.
enum class LinkLayer : std::uint8_t {
    Ethernet   = 0,
    Infiniband = 1,
    Roce       = 2,
    NVLink     = 3,
    Pcie       = 4,
    Cxl        = 5,
};
inline constexpr std::size_t link_layer_count =
    std::meta::enumerators_of(^^LinkLayer).size();

[[nodiscard]] constexpr std::string_view
link_layer_name(LinkLayer L) noexcept {
    switch (L) {
        case LinkLayer::Ethernet:   return "Ethernet";
        case LinkLayer::Infiniband: return "Infiniband";
        case LinkLayer::Roce:       return "Roce";
        case LinkLayer::NVLink:     return "NVLink";
        case LinkLayer::Pcie:       return "Pcie";
        case LinkLayer::Cxl:        return "Cxl";
        default: return std::string_view{"<unknown LinkLayer>"};
    }
}

// PCIe generation.  Underlying value IS the generation number so a
// `static_cast<uint8_t>(PcieGen::Gen5) == 5` invariant holds — keeps
// arithmetic on the underlying value (e.g. encoding bandwidth-per-lane
// scaling) honest without requiring a parallel switch.
enum class PcieGen : std::uint8_t {
    None = 0,
    Gen1 = 1,
    Gen2 = 2,
    Gen3 = 3,
    Gen4 = 4,
    Gen5 = 5,
    Gen6 = 6,
};
inline constexpr std::size_t pcie_gen_count =
    std::meta::enumerators_of(^^PcieGen).size();

[[nodiscard]] constexpr std::string_view
pcie_gen_name(PcieGen G) noexcept {
    switch (G) {
        case PcieGen::None: return "None";
        case PcieGen::Gen1: return "Gen1";
        case PcieGen::Gen2: return "Gen2";
        case PcieGen::Gen3: return "Gen3";
        case PcieGen::Gen4: return "Gen4";
        case PcieGen::Gen5: return "Gen5";
        case PcieGen::Gen6: return "Gen6";
        default: return std::string_view{"<unknown PcieGen>"};
    }
}

// ────────────────────────────────────────────────────────────────────
// Refined-field aliases used across schemas
// ────────────────────────────────────────────────────────────────────

// Power-of-two ∧ ≤ 128 — applies to GPU warp_size (32 NVIDIA, 64 AMD)
// and CPU SIMD vector_lanes (4/8/16/32/64 depending on ISA).
using PowerOfTwoLane = safety::Refined<
    safety::all_of<safety::power_of_two, safety::bounded_above<std::uint16_t{128}>>,
    std::uint16_t>;
static_assert(sizeof(PowerOfTwoLane) == sizeof(std::uint16_t),
    "PowerOfTwoLane must EBO-collapse to uint16_t — Refined<all_of<...>, "
    "uint16_t> is regime-1 with empty element_type.");

// Hard hardware ceiling on GPU registers per thread — 255 on every
// shipped backend.  Pinned at type level so a hostile preset writer
// setting 999 is structurally rejected.
using ValidRegsPerThread = safety::Refined<
    safety::bounded_above<std::uint16_t{255}>, std::uint16_t>;
static_assert(sizeof(ValidRegsPerThread) == sizeof(std::uint16_t));

// Dimensionless [0, 1] utilization ratio — calibrated occupancy /
// efficiency / fraction-of-peak fields.
using ValidUtilization = safety::Refined<safety::in_range<0.0f, 1.0f>, float>;
static_assert(sizeof(ValidUtilization) == sizeof(float));

// MTU upper bound: jumbo-frame ceiling 9216 bytes (some NICs go higher
// to 10000 or 16128; 16384 leaves headroom for one bit of forward
// growth without becoming a generic "uint16_t can hold this" check).
using ValidMtu = safety::Refined<
    safety::bounded_above<std::uint16_t{16384}>, std::uint16_t>;
static_assert(sizeof(ValidMtu) == sizeof(std::uint16_t));

// ────────────────────────────────────────────────────────────────────
// Feature enums (FOUND-I04 frozen-position bit catalogs)
// ────────────────────────────────────────────────────────────────────

// GpuFeature — one bit per GPU-die capability.  Underlying type uint32_t
// gives 32 bit positions; current catalog uses 9; the file-foot pin
// block freezes each bit position.
enum class GpuFeature : std::uint32_t {
    Tma             = 1u <<  0,  // Tensor Memory Accelerator (Hopper+)
    ClusterLaunch   = 1u <<  1,  // Thread Block Cluster (Hopper+)
    Fp8             = 1u <<  2,  // FP8 tensor-core ops (Hopper+)
    Bf16            = 1u <<  3,  // BF16 tensor-core ops (Ampere+)
    Tf32            = 1u <<  4,  // TF32 tensor-core ops (Ampere+)
    NvlinkSharp     = 1u <<  5,  // In-NVLink reduction (Hopper+ NVLink4)
    GpuDirectRdma   = 1u <<  6,  // GPU↔NIC peer DMA (Tesla+)
    GpuDirectStorage= 1u <<  7,  // GPU↔NVMe peer DMA (Magnum IO)
    Mig             = 1u <<  8,  // Multi-Instance GPU partitioning
};

// NicFeature — NIC capability catalog.  uint32_t for forward growth.
enum class NicFeature : std::uint32_t {
    Tso             = 1u <<  0,  // TCP segmentation offload
    Gso             = 1u <<  1,  // Generic segmentation offload
    Gro             = 1u <<  2,  // Generic receive offload
    Lro             = 1u <<  3,  // Large receive offload
    Rss             = 1u <<  4,  // Receive-side scaling
    Roce            = 1u <<  5,  // RDMA over Converged Ethernet
    Iwarp           = 1u <<  6,  // iWARP (deprecated; legacy fleets)
    KtlsOffload     = 1u <<  7,  // Kernel TLS offload (NIC AES)
    GpuDirectRdma   = 1u <<  8,  // GPU↔NIC peer DMA paired with GPU side
    XdpNative       = 1u <<  9,  // Native XDP (driver-side eBPF)
    XdpOffload      = 1u << 10,  // XDP offload to NIC ASIC
    AfXdp           = 1u << 11,  // AF_XDP zero-copy userspace transport
    SrIov           = 1u << 12,  // SR-IOV virtual functions
    Macsec          = 1u << 13,  // 802.1AE MAC encryption
    Ipsec           = 1u << 14,  // IPsec hardware offload
    TimestampingHw  = 1u << 15,  // Hardware PTP timestamping
    TcEbpf          = 1u << 16,  // TC clsact direct-action eBPF
};

// SwitchFeature — switch-port capability catalog.  uint16_t.
enum class SwitchFeature : std::uint16_t {
    Sharp           = 1u <<  0,  // In-network reduction (Mellanox SHARP)
    P4              = 1u <<  1,  // Programmable dataplane (Tofino, Spectrum)
    AdaptiveRouting = 1u <<  2,  // UEC-style adaptive routing
    Ecn             = 1u <<  3,  // Explicit Congestion Notification
    Pfc             = 1u <<  4,  // Priority Flow Control (lossless fabric)
    Tcam            = 1u <<  5,  // Hardware ACL/flow rules
    PortMirror      = 1u <<  6,  // SPAN / port mirroring
    Doca            = 1u <<  7,  // BlueField DPU offload (NVIDIA)
};

// CpuFeature — host-CPU ISA-feature catalog.  Covers x86-64 (Intel +
// AMD) and AArch64 (Graviton, Apple, Ampere).
enum class CpuFeature : std::uint32_t {
    Avx2            = 1u <<  0,  // AVX2 256-bit SIMD
    Avx512          = 1u <<  1,  // AVX-512 512-bit SIMD
    Amx             = 1u <<  2,  // Intel AMX tile matrix multiply
    Vnni            = 1u <<  3,  // VNNI int8 dot-product
    Bf16Cpu         = 1u <<  4,  // BF16 native (Cooper Lake / Sapphire Rapids+)
    Fp16Cpu         = 1u <<  5,  // FP16 native (Sapphire Rapids+)
    Aes             = 1u <<  6,  // AES-NI hardware AES
    Sha             = 1u <<  7,  // SHA-NI hardware SHA
    Neon            = 1u <<  8,  // ARM NEON 128-bit SIMD
    Sve             = 1u <<  9,  // ARM SVE scalable vector extension
    Sve2            = 1u << 10,  // SVE2 (ARMv9)
    Sme             = 1u << 11,  // ARM SME scalable matrix extension
    AmxBf16Arm      = 1u << 12,  // Apple AMX BF16
    Mte             = 1u << 13,  // ARM Memory Tagging Extension
    PauthArm        = 1u << 14,  // ARM Pointer Authentication
    Cet             = 1u << 15,  // Intel Control-flow Enforcement
};

// DramFeature — DRAM channel capability catalog.  uint8_t.
enum class DramFeature : std::uint8_t {
    Ecc             = 1u << 0,  // ECC support
    OnDieEcc        = 1u << 1,  // On-die ECC (DDR5+)
    PowerDownIdle   = 1u << 2,  // Self-refresh power-down idle
    Hbm             = 1u << 3,  // HBM stack (vs DDR/LPDDR)
};

// ── Diagnostic name accessors (constexpr for runtime-smoke discipline) ─

[[nodiscard]] constexpr std::string_view
gpu_feature_name(GpuFeature F) noexcept {
    switch (F) {
        case GpuFeature::Tma:              return "Tma";
        case GpuFeature::ClusterLaunch:    return "ClusterLaunch";
        case GpuFeature::Fp8:              return "Fp8";
        case GpuFeature::Bf16:             return "Bf16";
        case GpuFeature::Tf32:             return "Tf32";
        case GpuFeature::NvlinkSharp:      return "NvlinkSharp";
        case GpuFeature::GpuDirectRdma:    return "GpuDirectRdma";
        case GpuFeature::GpuDirectStorage: return "GpuDirectStorage";
        case GpuFeature::Mig:              return "Mig";
        default: return std::string_view{"<unknown GpuFeature>"};
    }
}

[[nodiscard]] constexpr std::string_view
nic_feature_name(NicFeature F) noexcept {
    switch (F) {
        case NicFeature::Tso:            return "Tso";
        case NicFeature::Gso:            return "Gso";
        case NicFeature::Gro:            return "Gro";
        case NicFeature::Lro:            return "Lro";
        case NicFeature::Rss:            return "Rss";
        case NicFeature::Roce:           return "Roce";
        case NicFeature::Iwarp:          return "Iwarp";
        case NicFeature::KtlsOffload:    return "KtlsOffload";
        case NicFeature::GpuDirectRdma:  return "GpuDirectRdma";
        case NicFeature::XdpNative:      return "XdpNative";
        case NicFeature::XdpOffload:     return "XdpOffload";
        case NicFeature::AfXdp:          return "AfXdp";
        case NicFeature::SrIov:          return "SrIov";
        case NicFeature::Macsec:         return "Macsec";
        case NicFeature::Ipsec:          return "Ipsec";
        case NicFeature::TimestampingHw: return "TimestampingHw";
        case NicFeature::TcEbpf:         return "TcEbpf";
        default: return std::string_view{"<unknown NicFeature>"};
    }
}

[[nodiscard]] constexpr std::string_view
switch_feature_name(SwitchFeature F) noexcept {
    switch (F) {
        case SwitchFeature::Sharp:           return "Sharp";
        case SwitchFeature::P4:              return "P4";
        case SwitchFeature::AdaptiveRouting: return "AdaptiveRouting";
        case SwitchFeature::Ecn:             return "Ecn";
        case SwitchFeature::Pfc:             return "Pfc";
        case SwitchFeature::Tcam:            return "Tcam";
        case SwitchFeature::PortMirror:      return "PortMirror";
        case SwitchFeature::Doca:            return "Doca";
        default: return std::string_view{"<unknown SwitchFeature>"};
    }
}

[[nodiscard]] constexpr std::string_view
cpu_feature_name(CpuFeature F) noexcept {
    switch (F) {
        case CpuFeature::Avx2:       return "Avx2";
        case CpuFeature::Avx512:     return "Avx512";
        case CpuFeature::Amx:        return "Amx";
        case CpuFeature::Vnni:       return "Vnni";
        case CpuFeature::Bf16Cpu:    return "Bf16Cpu";
        case CpuFeature::Fp16Cpu:    return "Fp16Cpu";
        case CpuFeature::Aes:        return "Aes";
        case CpuFeature::Sha:        return "Sha";
        case CpuFeature::Neon:       return "Neon";
        case CpuFeature::Sve:        return "Sve";
        case CpuFeature::Sve2:       return "Sve2";
        case CpuFeature::Sme:        return "Sme";
        case CpuFeature::AmxBf16Arm: return "AmxBf16Arm";
        case CpuFeature::Mte:        return "Mte";
        case CpuFeature::PauthArm:   return "PauthArm";
        case CpuFeature::Cet:        return "Cet";
        default: return std::string_view{"<unknown CpuFeature>"};
    }
}

[[nodiscard]] constexpr std::string_view
dram_feature_name(DramFeature F) noexcept {
    switch (F) {
        case DramFeature::Ecc:           return "Ecc";
        case DramFeature::OnDieEcc:      return "OnDieEcc";
        case DramFeature::PowerDownIdle: return "PowerDownIdle";
        case DramFeature::Hbm:           return "Hbm";
        default: return std::string_view{"<unknown DramFeature>"};
    }
}

// ────────────────────────────────────────────────────────────────────
// Schema 1: GpuTargetCaps  (CogKind::Gpu — L0 atomic compute substrate)
// ────────────────────────────────────────────────────────────────────

struct GpuTargetCaps {
    // ── Compute fabric (vendor datasheet) ──────────────────────────
    safety::Tagged<std::uint16_t, safety::source::Vendor>
        sm_count{std::uint16_t{0}};
    PowerOfTwoLane warp_size{std::uint16_t{32}};
    safety::Tagged<std::uint16_t, safety::source::Vendor>
        warp_schedulers_per_sm{std::uint16_t{0}};
    safety::Tagged<std::uint16_t, safety::source::Vendor>
        max_warps_per_sm{std::uint16_t{0}};
    ValidRegsPerThread max_regs_per_thread{std::uint16_t{255}};

    // ── Per-SM on-chip resources (vendor datasheet, bytes) ──────────
    safety::Tagged<std::uint32_t, safety::source::Vendor>
        registers_per_sm_bytes{std::uint32_t{0}};   // 256 KB on H100/B200
    safety::Tagged<std::uint32_t, safety::source::Vendor>
        smem_per_sm_bytes{std::uint32_t{0}};        // 228 KB on Hopper
    safety::Tagged<std::uint32_t, safety::source::Vendor>
        l1_per_sm_bytes{std::uint32_t{0}};          // 256 KB shared L1+smem
    safety::Tagged<std::uint32_t, safety::source::Vendor>
        tmem_per_sm_bytes{std::uint32_t{0}};        // 64 KB on Blackwell

    // ── Global memory (vendor datasheet) ───────────────────────────
    safety::Tagged<std::uint64_t, safety::source::Vendor>
        l2_bytes{std::uint64_t{0}};
    safety::Tagged<std::uint64_t, safety::source::Vendor>
        hbm_bytes{std::uint64_t{0}};
    safety::Tagged<std::uint64_t, safety::source::Vendor>
        hbm_bandwidth_bytes_per_sec{std::uint64_t{0}};

    // ── Inter-device link (vendor datasheet) ───────────────────────
    safety::Tagged<std::uint16_t, safety::source::Vendor>
        nvlink_lanes{std::uint16_t{0}};
    safety::Tagged<std::uint64_t, safety::source::Vendor>
        nvlink_bandwidth_bytes_per_sec{std::uint64_t{0}};
    safety::Tagged<PcieGen, safety::source::Vendor>
        pcie_gen{PcieGen::None};
    safety::Tagged<std::uint8_t, safety::source::Vendor>
        pcie_lanes{std::uint8_t{0}};

    // ── Calibrated peak throughput (TFLOPS, runtime measured) ──────
    safety::Tagged<float, safety::source::Calibrated> tflops_fp64{0.0f};
    safety::Tagged<float, safety::source::Calibrated> tflops_fp32{0.0f};
    safety::Tagged<float, safety::source::Calibrated> tflops_tf32{0.0f};
    safety::Tagged<float, safety::source::Calibrated> tflops_fp16{0.0f};
    safety::Tagged<float, safety::source::Calibrated> tflops_bf16{0.0f};
    safety::Tagged<float, safety::source::Calibrated> tflops_fp8{0.0f};
    safety::Tagged<float, safety::source::Calibrated> tflops_fp4{0.0f};
    safety::Tagged<float, safety::source::Calibrated> tops_int8{0.0f};

    // ── Power / thermal envelope ───────────────────────────────────
    safety::Tagged<std::uint16_t, safety::source::Vendor>
        tdp_watts{std::uint16_t{0}};
    safety::Tagged<std::uint16_t, safety::source::Vendor>
        thermal_throttle_celsius{std::uint16_t{0}};

    // ── Architecture identity ──────────────────────────────────────
    // SM version (e.g. 90 = Hopper, 100 = Blackwell).  Tagged Vendor.
    safety::Tagged<std::uint16_t, safety::source::Vendor>
        sm_version{std::uint16_t{0}};

    // ── Feature flags ──────────────────────────────────────────────
    safety::Bits<GpuFeature> features{};
};

// ────────────────────────────────────────────────────────────────────
// Schema 2: NicPortTargetCaps  (CogKind::NicPort — L0 atomic network)
// ────────────────────────────────────────────────────────────────────

struct NicPortTargetCaps {
    // ── Link layer + line rate (vendor datasheet) ──────────────────
    safety::Tagged<LinkLayer, safety::source::Vendor>
        link_layer{LinkLayer::Ethernet};
    safety::Tagged<std::uint64_t, safety::source::Vendor>
        line_rate_bytes_per_sec{std::uint64_t{0}};
    ValidMtu mtu_bytes{std::uint16_t{1500}};

    // ── Queue depth ceilings (vendor datasheet) ────────────────────
    safety::Tagged<std::uint16_t, safety::source::Vendor>
        max_tx_queues{std::uint16_t{0}};
    safety::Tagged<std::uint16_t, safety::source::Vendor>
        max_rx_queues{std::uint16_t{0}};
    safety::Tagged<std::uint32_t, safety::source::Vendor>
        max_qp_count{std::uint32_t{0}};       // RDMA queue pairs
    safety::Tagged<std::uint32_t, safety::source::Vendor>
        max_cq_count{std::uint32_t{0}};       // RDMA completion queues
    safety::Tagged<std::uint32_t, safety::source::Vendor>
        max_mr_count{std::uint32_t{0}};       // RDMA memory regions
    safety::Tagged<std::uint64_t, safety::source::Vendor>
        max_mr_size_bytes{std::uint64_t{0}};

    // ── Calibrated effective throughput (runtime measured) ─────────
    // Real-world line rate after PCIe / driver / kernel overhead.
    safety::Tagged<std::uint64_t, safety::source::Calibrated>
        effective_bandwidth_bytes_per_sec{std::uint64_t{0}};
    // sysctl-imposed ceiling (net.core.rmem_max-derived BDP cap).
    safety::Tagged<std::uint64_t, safety::source::Calibrated>
        sysctl_throughput_ceiling_bytes_per_sec{std::uint64_t{0}};
    // Bandwidth-Delay-Product ceiling at this NIC's measured RTT.
    safety::Tagged<std::uint64_t, safety::source::Calibrated>
        bdp_ceiling_bytes_per_sec{std::uint64_t{0}};

    // ── Topology placement (vendor datasheet) ──────────────────────
    safety::Tagged<std::uint16_t, safety::source::Vendor>
        pcie_root_complex_id{std::uint16_t{0}};
    safety::Tagged<PcieGen, safety::source::Vendor>
        pcie_gen{PcieGen::None};
    safety::Tagged<std::uint8_t, safety::source::Vendor>
        pcie_lanes{std::uint8_t{0}};

    // ── GPUDirect peer GPUs reachable via this port ────────────────
    // Span borrowed from an external arena (not owned by this struct).
    // Empty default = no peers / not yet discovered.
    std::span<const CogIdentity> gpu_direct_peers{};

    // ── Feature flags ──────────────────────────────────────────────
    safety::Bits<NicFeature> features{};
};

// ────────────────────────────────────────────────────────────────────
// Schema 3: NvSwitchTargetCaps  (CogKind::NvSwitch — L0 fabric atom)
// ────────────────────────────────────────────────────────────────────

struct NvSwitchTargetCaps {
    // ── Port topology (vendor datasheet) ───────────────────────────
    safety::Tagged<std::uint16_t, safety::source::Vendor>
        port_count{std::uint16_t{0}};
    safety::Tagged<std::uint64_t, safety::source::Vendor>
        per_port_bandwidth_bytes_per_sec{std::uint64_t{0}};
    safety::Tagged<std::uint64_t, safety::source::Vendor>
        aggregate_bandwidth_bytes_per_sec{std::uint64_t{0}};

    // ── Buffering + ACL (vendor datasheet) ─────────────────────────
    safety::Tagged<std::uint64_t, safety::source::Vendor>
        buffer_bytes{std::uint64_t{0}};
    safety::Tagged<std::uint32_t, safety::source::Vendor>
        tcam_entries{std::uint32_t{0}};

    // ── Calibrated effective bandwidth (runtime measured) ──────────
    safety::Tagged<std::uint64_t, safety::source::Calibrated>
        effective_aggregate_bytes_per_sec{std::uint64_t{0}};

    // ── Feature flags ──────────────────────────────────────────────
    safety::Bits<SwitchFeature> features{};
};

// ────────────────────────────────────────────────────────────────────
// Schema 4: CpuCoreTargetCaps + CpuSocketTargetCaps
//           (CogKind::CpuCore — L0 atomic, CogKind::CpuSocket — L1)
// ────────────────────────────────────────────────────────────────────

struct CpuCoreTargetCaps {
    // ── Per-core ISA + clock (vendor datasheet) ────────────────────
    safety::Tagged<std::uint32_t, safety::source::Vendor>
        base_clock_mhz{std::uint32_t{0}};
    safety::Tagged<std::uint32_t, safety::source::Vendor>
        max_clock_mhz{std::uint32_t{0}};
    PowerOfTwoLane simd_vector_lanes{std::uint16_t{8}};

    // ── Per-core caches (vendor datasheet, bytes) ──────────────────
    safety::Tagged<std::uint32_t, safety::source::Vendor>
        l1d_bytes{std::uint32_t{0}};
    safety::Tagged<std::uint32_t, safety::source::Vendor>
        l1i_bytes{std::uint32_t{0}};
    safety::Tagged<std::uint32_t, safety::source::Vendor>
        l2_bytes{std::uint32_t{0}};

    // ── Calibrated peak throughput (TFLOPS, runtime measured) ──────
    safety::Tagged<float, safety::source::Calibrated> tflops_fp64{0.0f};
    safety::Tagged<float, safety::source::Calibrated> tflops_fp32{0.0f};
    safety::Tagged<float, safety::source::Calibrated> tflops_bf16{0.0f};

    // ── Feature flags ──────────────────────────────────────────────
    safety::Bits<CpuFeature> features{};
};

struct CpuSocketTargetCaps {
    // ── Core composition (vendor datasheet) ────────────────────────
    safety::Tagged<std::uint16_t, safety::source::Vendor>
        core_count{std::uint16_t{0}};
    safety::Tagged<std::uint16_t, safety::source::Vendor>
        thread_count{std::uint16_t{0}};

    // ── Shared cache / memory (vendor datasheet) ───────────────────
    safety::Tagged<std::uint64_t, safety::source::Vendor>
        l3_bytes{std::uint64_t{0}};
    safety::Tagged<std::uint8_t, safety::source::Vendor>
        numa_node_count{std::uint8_t{1}};

    // ── Per-socket peak (calibrated) ───────────────────────────────
    safety::Tagged<float, safety::source::Calibrated>
        memory_bandwidth_bytes_per_sec_per_socket{0.0f};

    // ── Power / thermal envelope ───────────────────────────────────
    safety::Tagged<std::uint16_t, safety::source::Vendor>
        tdp_watts{std::uint16_t{0}};
    safety::Tagged<std::uint16_t, safety::source::Vendor>
        thermal_throttle_celsius{std::uint16_t{0}};

    // ── Per-core descriptor (shared across cores in this socket) ───
    CpuCoreTargetCaps representative_core{};

    // ── Feature flags (socket-aggregated; same enum as per-core) ───
    safety::Bits<CpuFeature> features{};
};

// ────────────────────────────────────────────────────────────────────
// Schema 5: DramChannelTargetCaps  (CogKind::DramChannel — L0 atomic)
// ────────────────────────────────────────────────────────────────────

struct DramChannelTargetCaps {
    // ── Channel geometry (vendor datasheet) ────────────────────────
    safety::Tagged<std::uint8_t, safety::source::Vendor>
        channel_width_bits{std::uint8_t{64}};      // 32 / 64 / 128
    safety::Tagged<std::uint16_t, safety::source::Vendor>
        speed_mts{std::uint16_t{0}};               // mega-transfers/s

    // ── Calibrated bandwidth (runtime measured, bytes/sec) ─────────
    safety::Tagged<std::uint64_t, safety::source::Calibrated>
        bandwidth_bytes_per_sec{std::uint64_t{0}};

    // ── Capacity (vendor datasheet) ────────────────────────────────
    safety::Tagged<std::uint64_t, safety::source::Vendor>
        capacity_bytes{std::uint64_t{0}};

    // ── Feature flags ──────────────────────────────────────────────
    safety::Bits<DramFeature> features{};
};

// ────────────────────────────────────────────────────────────────────
// caps_for<CogKind> — kind-to-schema binding metafunction
// ────────────────────────────────────────────────────────────────────

// Primary template — INTENTIONALLY UNDEFINED.  Reaching here means the
// caller asked `caps_for_t<K>` for a CogKind that has no schema (e.g.
// PsuRail, BmcSensor — non-schedulable Cogs).  Compile error names the
// concept gate failure.
template <CogKind K>
struct caps_for;

template <> struct caps_for<CogKind::Gpu>          { using type = GpuTargetCaps;          };
template <> struct caps_for<CogKind::CpuCore>      { using type = CpuCoreTargetCaps;      };
template <> struct caps_for<CogKind::CpuSocket>    { using type = CpuSocketTargetCaps;    };
template <> struct caps_for<CogKind::NicPort>      { using type = NicPortTargetCaps;      };
template <> struct caps_for<CogKind::NvSwitch>     { using type = NvSwitchTargetCaps;     };
template <> struct caps_for<CogKind::DramChannel>  { using type = DramChannelTargetCaps;  };

template <CogKind K>
using caps_for_t = typename caps_for<K>::type;

// HasCaps<K> — concept gate.  Constrained templates downstream (Mimic
// factories, FitsCog, OpcodeLatencyTable) refuse non-substrate kinds at
// substitution time per HS14.
template <CogKind K>
concept HasCaps = requires { typename caps_for<K>::type; };

// ────────────────────────────────────────────────────────────────────
// Self-test block — name coverage + FOUND-I04 frozen-position pins
// ────────────────────────────────────────────────────────────────────

namespace detail::target_caps_self_test {

// Sizeof sanity — every Tagged<T, Vendor>/Tagged<T, Calibrated> must
// EBO-collapse to sizeof(T).  Spot-check the load-bearing field types
// to catch silent regression in safety/Tagged.h.
static_assert(sizeof(safety::Tagged<std::uint16_t, safety::source::Vendor>)
              == sizeof(std::uint16_t),
    "Tagged<u16, source::Vendor> must EBO-collapse — TrustLattice has "
    "empty element_type so the wrapper carries no extra storage.");
static_assert(sizeof(safety::Tagged<std::uint64_t, safety::source::Calibrated>)
              == sizeof(std::uint64_t));
static_assert(sizeof(safety::Tagged<float, safety::source::Calibrated>)
              == sizeof(float));

// Bits sizeof for each feature enum — one byte / two / four matching
// the underlying type, EBO-collapsed inside the schema struct.
static_assert(sizeof(safety::Bits<GpuFeature>)    == sizeof(std::uint32_t));
static_assert(sizeof(safety::Bits<NicFeature>)    == sizeof(std::uint32_t));
static_assert(sizeof(safety::Bits<SwitchFeature>) == sizeof(std::uint16_t));
static_assert(sizeof(safety::Bits<CpuFeature>)    == sizeof(std::uint32_t));
static_assert(sizeof(safety::Bits<DramFeature>)   == sizeof(std::uint8_t));

// ── Reflection-driven name coverage (per FOUND-I04 discipline) ─────
//
// Every enumerator MUST have a non-sentinel name from the corresponding
// *_name accessor.  Adding a new enumerator without updating the switch
// fires the matching assertion at header-inclusion time, naming the
// missing arm.

[[nodiscard]] consteval bool every_link_layer_has_name() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^LinkLayer));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (link_layer_name([:en:]) ==
            std::string_view{"<unknown LinkLayer>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_link_layer_has_name(),
    "link_layer_name() switch is missing an arm for at least one "
    "LinkLayer atom.");

[[nodiscard]] consteval bool every_pcie_gen_has_name() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^PcieGen));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (pcie_gen_name([:en:]) ==
            std::string_view{"<unknown PcieGen>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_pcie_gen_has_name());

[[nodiscard]] consteval bool every_gpu_feature_has_name() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^GpuFeature));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (gpu_feature_name([:en:]) ==
            std::string_view{"<unknown GpuFeature>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_gpu_feature_has_name());

[[nodiscard]] consteval bool every_nic_feature_has_name() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^NicFeature));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (nic_feature_name([:en:]) ==
            std::string_view{"<unknown NicFeature>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_nic_feature_has_name());

[[nodiscard]] consteval bool every_switch_feature_has_name() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^SwitchFeature));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (switch_feature_name([:en:]) ==
            std::string_view{"<unknown SwitchFeature>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_switch_feature_has_name());

[[nodiscard]] consteval bool every_cpu_feature_has_name() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^CpuFeature));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (cpu_feature_name([:en:]) ==
            std::string_view{"<unknown CpuFeature>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_cpu_feature_has_name());

[[nodiscard]] consteval bool every_dram_feature_has_name() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^DramFeature));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (dram_feature_name([:en:]) ==
            std::string_view{"<unknown DramFeature>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_dram_feature_has_name());

// ── FOUND-I04 frozen-position pins ─────────────────────────────────
//
// LinkLayer
static_assert(static_cast<std::uint8_t>(LinkLayer::Ethernet)   == 0,
    "LinkLayer::Ethernet underlying value drifted — federation row_hash "
    "invalidated.  Pin lives in cog/TargetCaps.h.");
static_assert(static_cast<std::uint8_t>(LinkLayer::Infiniband) == 1);
static_assert(static_cast<std::uint8_t>(LinkLayer::Roce)       == 2);
static_assert(static_cast<std::uint8_t>(LinkLayer::NVLink)     == 3);
static_assert(static_cast<std::uint8_t>(LinkLayer::Pcie)       == 4);
static_assert(static_cast<std::uint8_t>(LinkLayer::Cxl)        == 5);

// PcieGen — underlying value IS the generation number
static_assert(static_cast<std::uint8_t>(PcieGen::None) == 0);
static_assert(static_cast<std::uint8_t>(PcieGen::Gen1) == 1);
static_assert(static_cast<std::uint8_t>(PcieGen::Gen2) == 2);
static_assert(static_cast<std::uint8_t>(PcieGen::Gen3) == 3);
static_assert(static_cast<std::uint8_t>(PcieGen::Gen4) == 4);
static_assert(static_cast<std::uint8_t>(PcieGen::Gen5) == 5);
static_assert(static_cast<std::uint8_t>(PcieGen::Gen6) == 6);

// GpuFeature — bit-position pins
static_assert(static_cast<std::uint32_t>(GpuFeature::Tma)              == (1u << 0));
static_assert(static_cast<std::uint32_t>(GpuFeature::ClusterLaunch)    == (1u << 1));
static_assert(static_cast<std::uint32_t>(GpuFeature::Fp8)              == (1u << 2));
static_assert(static_cast<std::uint32_t>(GpuFeature::Bf16)             == (1u << 3));
static_assert(static_cast<std::uint32_t>(GpuFeature::Tf32)             == (1u << 4));
static_assert(static_cast<std::uint32_t>(GpuFeature::NvlinkSharp)      == (1u << 5));
static_assert(static_cast<std::uint32_t>(GpuFeature::GpuDirectRdma)    == (1u << 6));
static_assert(static_cast<std::uint32_t>(GpuFeature::GpuDirectStorage) == (1u << 7));
static_assert(static_cast<std::uint32_t>(GpuFeature::Mig)              == (1u << 8));

// NicFeature — bit-position pins
static_assert(static_cast<std::uint32_t>(NicFeature::Tso)            == (1u <<  0));
static_assert(static_cast<std::uint32_t>(NicFeature::Gso)            == (1u <<  1));
static_assert(static_cast<std::uint32_t>(NicFeature::Gro)            == (1u <<  2));
static_assert(static_cast<std::uint32_t>(NicFeature::Lro)            == (1u <<  3));
static_assert(static_cast<std::uint32_t>(NicFeature::Rss)            == (1u <<  4));
static_assert(static_cast<std::uint32_t>(NicFeature::Roce)           == (1u <<  5));
static_assert(static_cast<std::uint32_t>(NicFeature::Iwarp)          == (1u <<  6));
static_assert(static_cast<std::uint32_t>(NicFeature::KtlsOffload)    == (1u <<  7));
static_assert(static_cast<std::uint32_t>(NicFeature::GpuDirectRdma)  == (1u <<  8));
static_assert(static_cast<std::uint32_t>(NicFeature::XdpNative)      == (1u <<  9));
static_assert(static_cast<std::uint32_t>(NicFeature::XdpOffload)     == (1u << 10));
static_assert(static_cast<std::uint32_t>(NicFeature::AfXdp)          == (1u << 11));
static_assert(static_cast<std::uint32_t>(NicFeature::SrIov)          == (1u << 12));
static_assert(static_cast<std::uint32_t>(NicFeature::Macsec)         == (1u << 13));
static_assert(static_cast<std::uint32_t>(NicFeature::Ipsec)          == (1u << 14));
static_assert(static_cast<std::uint32_t>(NicFeature::TimestampingHw) == (1u << 15));
static_assert(static_cast<std::uint32_t>(NicFeature::TcEbpf)         == (1u << 16));

// SwitchFeature — bit-position pins
static_assert(static_cast<std::uint16_t>(SwitchFeature::Sharp)           == (1u << 0));
static_assert(static_cast<std::uint16_t>(SwitchFeature::P4)              == (1u << 1));
static_assert(static_cast<std::uint16_t>(SwitchFeature::AdaptiveRouting) == (1u << 2));
static_assert(static_cast<std::uint16_t>(SwitchFeature::Ecn)             == (1u << 3));
static_assert(static_cast<std::uint16_t>(SwitchFeature::Pfc)             == (1u << 4));
static_assert(static_cast<std::uint16_t>(SwitchFeature::Tcam)            == (1u << 5));
static_assert(static_cast<std::uint16_t>(SwitchFeature::PortMirror)      == (1u << 6));
static_assert(static_cast<std::uint16_t>(SwitchFeature::Doca)            == (1u << 7));

// CpuFeature — bit-position pins
static_assert(static_cast<std::uint32_t>(CpuFeature::Avx2)       == (1u <<  0));
static_assert(static_cast<std::uint32_t>(CpuFeature::Avx512)     == (1u <<  1));
static_assert(static_cast<std::uint32_t>(CpuFeature::Amx)        == (1u <<  2));
static_assert(static_cast<std::uint32_t>(CpuFeature::Vnni)       == (1u <<  3));
static_assert(static_cast<std::uint32_t>(CpuFeature::Bf16Cpu)    == (1u <<  4));
static_assert(static_cast<std::uint32_t>(CpuFeature::Fp16Cpu)    == (1u <<  5));
static_assert(static_cast<std::uint32_t>(CpuFeature::Aes)        == (1u <<  6));
static_assert(static_cast<std::uint32_t>(CpuFeature::Sha)        == (1u <<  7));
static_assert(static_cast<std::uint32_t>(CpuFeature::Neon)       == (1u <<  8));
static_assert(static_cast<std::uint32_t>(CpuFeature::Sve)        == (1u <<  9));
static_assert(static_cast<std::uint32_t>(CpuFeature::Sve2)       == (1u << 10));
static_assert(static_cast<std::uint32_t>(CpuFeature::Sme)        == (1u << 11));
static_assert(static_cast<std::uint32_t>(CpuFeature::AmxBf16Arm) == (1u << 12));
static_assert(static_cast<std::uint32_t>(CpuFeature::Mte)        == (1u << 13));
static_assert(static_cast<std::uint32_t>(CpuFeature::PauthArm)   == (1u << 14));
static_assert(static_cast<std::uint32_t>(CpuFeature::Cet)        == (1u << 15));

// DramFeature — bit-position pins
static_assert(static_cast<std::uint8_t>(DramFeature::Ecc)           == (1u << 0));
static_assert(static_cast<std::uint8_t>(DramFeature::OnDieEcc)      == (1u << 1));
static_assert(static_cast<std::uint8_t>(DramFeature::PowerDownIdle) == (1u << 2));
static_assert(static_cast<std::uint8_t>(DramFeature::Hbm)           == (1u << 3));

// Underlying-type pins — ABI-breaking widen would silently re-shape
// every TargetCaps that embeds Bits<F> by value.
static_assert(std::is_same_v<std::underlying_type_t<LinkLayer>,     std::uint8_t>);
static_assert(std::is_same_v<std::underlying_type_t<PcieGen>,       std::uint8_t>);
static_assert(std::is_same_v<std::underlying_type_t<GpuFeature>,    std::uint32_t>);
static_assert(std::is_same_v<std::underlying_type_t<NicFeature>,    std::uint32_t>);
static_assert(std::is_same_v<std::underlying_type_t<SwitchFeature>, std::uint16_t>);
static_assert(std::is_same_v<std::underlying_type_t<CpuFeature>,    std::uint32_t>);
static_assert(std::is_same_v<std::underlying_type_t<DramFeature>,   std::uint8_t>);

// ── caps_for binding sanity ────────────────────────────────────────
//
// Every specialised CogKind has the matching schema bound.  Cross-
// binding mismatch (caps_for<Gpu> resolving to NicPortTargetCaps,
// caps_for<NicPort> resolving to GpuTargetCaps) would silently route a
// GPU kernel through the network schema — instant catastrophic
// confusion.  Pin the bindings here.
static_assert(std::is_same_v<caps_for_t<CogKind::Gpu>,         GpuTargetCaps>);
static_assert(std::is_same_v<caps_for_t<CogKind::CpuCore>,     CpuCoreTargetCaps>);
static_assert(std::is_same_v<caps_for_t<CogKind::CpuSocket>,   CpuSocketTargetCaps>);
static_assert(std::is_same_v<caps_for_t<CogKind::NicPort>,     NicPortTargetCaps>);
static_assert(std::is_same_v<caps_for_t<CogKind::NvSwitch>,    NvSwitchTargetCaps>);
static_assert(std::is_same_v<caps_for_t<CogKind::DramChannel>, DramChannelTargetCaps>);

// HasCaps<K> — the schedulable substrates satisfy; non-schedulable
// kinds (PsuRail, BmcSensor, the L2..L7 aggregates) do NOT.  HS14
// fixture #1 in test/cog_neg/ witnesses the rejection.
static_assert(HasCaps<CogKind::Gpu>);
static_assert(HasCaps<CogKind::CpuCore>);
static_assert(HasCaps<CogKind::CpuSocket>);
static_assert(HasCaps<CogKind::NicPort>);
static_assert(HasCaps<CogKind::NvSwitch>);
static_assert(HasCaps<CogKind::DramChannel>);
static_assert(!HasCaps<CogKind::PsuRail>);
static_assert(!HasCaps<CogKind::BmcSensor>);
static_assert(!HasCaps<CogKind::Datacenter>);

// ── Layout sanity for downstream ABI ───────────────────────────────
//
// Standard-layout requirement — TargetCaps schemas may be serialized
// to disk (Cipher) or shipped over the wire (federation snapshot).
// Standard-layout means no virtual functions / non-public members /
// multiple-base-class layout — which the schemas trivially satisfy
// (they're aggregates of trivially-copyable wrappers).  Layout
// compatibility test guards against future "let's add a method via
// inheritance" temptations that would break the wire format.
static_assert(std::is_standard_layout_v<GpuTargetCaps>);
static_assert(std::is_standard_layout_v<NicPortTargetCaps>);
static_assert(std::is_standard_layout_v<NvSwitchTargetCaps>);
static_assert(std::is_standard_layout_v<CpuCoreTargetCaps>);
static_assert(std::is_standard_layout_v<CpuSocketTargetCaps>);
static_assert(std::is_standard_layout_v<DramChannelTargetCaps>);

}  // namespace detail::target_caps_self_test

}  // namespace crucible::cog
