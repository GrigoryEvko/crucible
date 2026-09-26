#pragma once

#include <crucible/cog/CogIdentity.h>
#include <fixy/Bits.h>
#include <fixy/Refined.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>

#include <cstdint>
#include <meta>
#include <span>
#include <string_view>
#include <type_traits>

namespace crucible::cog {

// Every enumerator below is frozen by underlying value. A schema is
// serialized into cached snapshots that outlive the process, so
// renumbering an atom silently reinterprets every snapshot that carries
// it. New atoms take the next free value or bit position.
enum class LinkLayer : std::uint8_t {
    Ethernet = 0,
    Infiniband = 1,
    Roce = 2,
    NVLink = 3,
    Pcie = 4,
    Cxl = 5,
};
inline constexpr std::size_t link_layer_count = std::meta::enumerators_of(^^LinkLayer).size();

[[nodiscard]] constexpr std::string_view link_layer_name(LinkLayer L) noexcept {
    switch (L) {
        case LinkLayer::Ethernet:
            return "Ethernet";
        case LinkLayer::Infiniband:
            return "Infiniband";
        case LinkLayer::Roce:
            return "Roce";
        case LinkLayer::NVLink:
            return "NVLink";
        case LinkLayer::Pcie:
            return "Pcie";
        case LinkLayer::Cxl:
            return "Cxl";
        default:
            return std::string_view{"<unknown LinkLayer>"};
    }
}

// The underlying value is the PCIe generation number itself, so
// bandwidth-per-lane scaling reads it directly.
enum class PcieGen : std::uint8_t {
    None = 0,
    Gen1 = 1,
    Gen2 = 2,
    Gen3 = 3,
    Gen4 = 4,
    Gen5 = 5,
    Gen6 = 6,
};
inline constexpr std::size_t pcie_gen_count = std::meta::enumerators_of(^^PcieGen).size();

[[nodiscard]] constexpr std::string_view pcie_gen_name(PcieGen G) noexcept {
    switch (G) {
        case PcieGen::None:
            return "None";
        case PcieGen::Gen1:
            return "Gen1";
        case PcieGen::Gen2:
            return "Gen2";
        case PcieGen::Gen3:
            return "Gen3";
        case PcieGen::Gen4:
            return "Gen4";
        case PcieGen::Gen5:
            return "Gen5";
        case PcieGen::Gen6:
            return "Gen6";
        default:
            return std::string_view{"<unknown PcieGen>"};
    }
}

// A figure measured on this host rather than read from the vendor.
template <typename T>
using CalibratedValue = ::fixy::Tagged<T, ::fixy::tags::source::Calibrated>;

// Each refinement below names its predicate, so a site that builds one
// writes ::fixy::mint_refined<predicate>(value) and the check runs there.

// A GPU warp is 32 lanes on NVIDIA and 64 on AMD. A CPU SIMD register
// holds 4 to 64 lanes depending on the ISA.
inline constexpr auto power_of_two_lane = ::fixy::all_of<::fixy::power_of_two, ::fixy::bounded_above<std::uint16_t{128}>>;
using PowerOfTwoLane = ::fixy::Refined<power_of_two_lane, std::uint16_t>;
static_assert(sizeof(PowerOfTwoLane) == sizeof(std::uint16_t),
              "PowerOfTwoLane must collapse to the size of the value it refines.");

// 255 is the architectural ceiling on registers per thread on every
// shipped GPU backend.
inline constexpr auto valid_regs_per_thread = ::fixy::bounded_above<std::uint16_t{255}>;
using ValidRegsPerThread = ::fixy::Refined<valid_regs_per_thread, std::uint16_t>;
static_assert(sizeof(ValidRegsPerThread) == sizeof(std::uint16_t));

inline constexpr auto valid_utilization = ::fixy::in_range<0.0f, 1.0f>;
using ValidUtilization = ::fixy::Refined<valid_utilization, float>;
static_assert(sizeof(ValidUtilization) == sizeof(float));

// The jumbo-frame ceiling is 9216 bytes, and some NICs accept 10000 or
// 16128. The bound is 16384 rather than the uint16_t maximum so that it
// still rejects a garbage value while leaving room for the next step up.
inline constexpr auto valid_mtu = ::fixy::bounded_above<std::uint16_t{16384}>;
using ValidMtu = ::fixy::Refined<valid_mtu, std::uint16_t>;
static_assert(sizeof(ValidMtu) == sizeof(std::uint16_t));

enum class GpuFeature : std::uint32_t {
    Tma = 1u << 0,  // Tensor Memory Accelerator
    ClusterLaunch = 1u << 1,  // Thread block cluster
    Fp8 = 1u << 2,  // FP8 tensor-core ops
    Bf16 = 1u << 3,  // BF16 tensor-core ops
    Tf32 = 1u << 4,  // TF32 tensor-core ops
    NvlinkSharp = 1u << 5,  // Reduction inside the NVLink fabric
    GpuDirectRdma = 1u << 6,  // Peer DMA between GPU and NIC
    GpuDirectStorage = 1u << 7,  // Peer DMA between GPU and NVMe
    Mig = 1u << 8,  // Multi-Instance GPU partitioning
};

enum class NicFeature : std::uint32_t {
    Tso = 1u << 0,  // TCP segmentation offload
    Gso = 1u << 1,  // Generic segmentation offload
    Gro = 1u << 2,  // Generic receive offload
    Lro = 1u << 3,  // Large receive offload
    Rss = 1u << 4,  // Receive-side scaling
    Roce = 1u << 5,  // RDMA over Converged Ethernet
    Iwarp = 1u << 6,  // iWARP RDMA, legacy fleets only
    KtlsOffload = 1u << 7,  // Kernel TLS offload to the NIC AES engine
    GpuDirectRdma = 1u << 8,  // Peer DMA between GPU and NIC
    XdpNative = 1u << 9,  // Driver-side eBPF
    XdpOffload = 1u << 10,  // XDP offload to the NIC ASIC
    AfXdp = 1u << 11,  // AF_XDP zero-copy userspace transport
    SrIov = 1u << 12,  // SR-IOV virtual functions
    Macsec = 1u << 13,  // 802.1AE MAC encryption
    Ipsec = 1u << 14,  // IPsec hardware offload
    TimestampingHw = 1u << 15,  // Hardware PTP timestamping
    TcEbpf = 1u << 16,  // TC clsact direct-action eBPF
    Tcam = 1u << 17,  // Hardware ACL and flow-steering TCAM
};

enum class SwitchFeature : std::uint16_t {
    Sharp = 1u << 0,  // In-network reduction
    P4 = 1u << 1,  // Programmable dataplane
    AdaptiveRouting = 1u << 2,
    Ecn = 1u << 3,  // Explicit Congestion Notification
    Pfc = 1u << 4,  // Priority Flow Control, lossless fabric
    Tcam = 1u << 5,  // Hardware ACL and flow rules
    PortMirror = 1u << 6,  // SPAN
    Doca = 1u << 7,  // BlueField DPU offload
};

// Covers both x86-64 and AArch64.
enum class CpuFeature : std::uint32_t {
    Avx2 = 1u << 0,  // 256-bit SIMD
    Avx512 = 1u << 1,  // 512-bit SIMD
    Amx = 1u << 2,  // Intel tile matrix multiply
    Vnni = 1u << 3,  // int8 dot-product
    Bf16Cpu = 1u << 4,
    Fp16Cpu = 1u << 5,
    Aes = 1u << 6,  // Hardware AES
    Sha = 1u << 7,  // Hardware SHA
    Neon = 1u << 8,  // ARM 128-bit SIMD
    Sve = 1u << 9,  // ARM scalable vector extension
    Sve2 = 1u << 10,
    Sme = 1u << 11,  // ARM scalable matrix extension
    AmxBf16Arm = 1u << 12,  // Apple tile matrix multiply, BF16
    Mte = 1u << 13,  // ARM Memory Tagging Extension
    PauthArm = 1u << 14,  // ARM Pointer Authentication
    Cet = 1u << 15,  // Intel Control-flow Enforcement
};

enum class DramFeature : std::uint8_t {
    Ecc = 1u << 0,
    OnDieEcc = 1u << 1,
    PowerDownIdle = 1u << 2,
    Hbm = 1u << 3,  // HBM stack rather than DDR or LPDDR
};

[[nodiscard]] constexpr std::string_view gpu_feature_name(GpuFeature F) noexcept {
    switch (F) {
        case GpuFeature::Tma:
            return "Tma";
        case GpuFeature::ClusterLaunch:
            return "ClusterLaunch";
        case GpuFeature::Fp8:
            return "Fp8";
        case GpuFeature::Bf16:
            return "Bf16";
        case GpuFeature::Tf32:
            return "Tf32";
        case GpuFeature::NvlinkSharp:
            return "NvlinkSharp";
        case GpuFeature::GpuDirectRdma:
            return "GpuDirectRdma";
        case GpuFeature::GpuDirectStorage:
            return "GpuDirectStorage";
        case GpuFeature::Mig:
            return "Mig";
        default:
            return std::string_view{"<unknown GpuFeature>"};
    }
}

[[nodiscard]] constexpr std::string_view nic_feature_name(NicFeature F) noexcept {
    switch (F) {
        case NicFeature::Tso:
            return "Tso";
        case NicFeature::Gso:
            return "Gso";
        case NicFeature::Gro:
            return "Gro";
        case NicFeature::Lro:
            return "Lro";
        case NicFeature::Rss:
            return "Rss";
        case NicFeature::Roce:
            return "Roce";
        case NicFeature::Iwarp:
            return "Iwarp";
        case NicFeature::KtlsOffload:
            return "KtlsOffload";
        case NicFeature::GpuDirectRdma:
            return "GpuDirectRdma";
        case NicFeature::XdpNative:
            return "XdpNative";
        case NicFeature::XdpOffload:
            return "XdpOffload";
        case NicFeature::AfXdp:
            return "AfXdp";
        case NicFeature::SrIov:
            return "SrIov";
        case NicFeature::Macsec:
            return "Macsec";
        case NicFeature::Ipsec:
            return "Ipsec";
        case NicFeature::TimestampingHw:
            return "TimestampingHw";
        case NicFeature::TcEbpf:
            return "TcEbpf";
        case NicFeature::Tcam:
            return "Tcam";
        default:
            return std::string_view{"<unknown NicFeature>"};
    }
}

[[nodiscard]] constexpr std::string_view switch_feature_name(SwitchFeature F) noexcept {
    switch (F) {
        case SwitchFeature::Sharp:
            return "Sharp";
        case SwitchFeature::P4:
            return "P4";
        case SwitchFeature::AdaptiveRouting:
            return "AdaptiveRouting";
        case SwitchFeature::Ecn:
            return "Ecn";
        case SwitchFeature::Pfc:
            return "Pfc";
        case SwitchFeature::Tcam:
            return "Tcam";
        case SwitchFeature::PortMirror:
            return "PortMirror";
        case SwitchFeature::Doca:
            return "Doca";
        default:
            return std::string_view{"<unknown SwitchFeature>"};
    }
}

[[nodiscard]] constexpr std::string_view cpu_feature_name(CpuFeature F) noexcept {
    switch (F) {
        case CpuFeature::Avx2:
            return "Avx2";
        case CpuFeature::Avx512:
            return "Avx512";
        case CpuFeature::Amx:
            return "Amx";
        case CpuFeature::Vnni:
            return "Vnni";
        case CpuFeature::Bf16Cpu:
            return "Bf16Cpu";
        case CpuFeature::Fp16Cpu:
            return "Fp16Cpu";
        case CpuFeature::Aes:
            return "Aes";
        case CpuFeature::Sha:
            return "Sha";
        case CpuFeature::Neon:
            return "Neon";
        case CpuFeature::Sve:
            return "Sve";
        case CpuFeature::Sve2:
            return "Sve2";
        case CpuFeature::Sme:
            return "Sme";
        case CpuFeature::AmxBf16Arm:
            return "AmxBf16Arm";
        case CpuFeature::Mte:
            return "Mte";
        case CpuFeature::PauthArm:
            return "PauthArm";
        case CpuFeature::Cet:
            return "Cet";
        default:
            return std::string_view{"<unknown CpuFeature>"};
    }
}

[[nodiscard]] constexpr std::string_view dram_feature_name(DramFeature F) noexcept {
    switch (F) {
        case DramFeature::Ecc:
            return "Ecc";
        case DramFeature::OnDieEcc:
            return "OnDieEcc";
        case DramFeature::PowerDownIdle:
            return "PowerDownIdle";
        case DramFeature::Hbm:
            return "Hbm";
        default:
            return std::string_view{"<unknown DramFeature>"};
    }
}

// A count field defaults to zero, the sentinel for "not yet
// discovered", so it carries no positivity refinement. Positivity is a
// post-condition of discovery, checked where the schema is filled in.
struct GpuTargetCaps {
    VendorClaim<std::uint16_t> sm_count{};
    PowerOfTwoLane warp_size = ::fixy::mint_refined<power_of_two_lane>(std::uint16_t{32});
    VendorClaim<std::uint16_t> warp_schedulers_per_sm{};
    VendorClaim<std::uint16_t> max_warps_per_sm{};
    ValidRegsPerThread max_regs_per_thread = ::fixy::mint_refined<valid_regs_per_thread>(std::uint16_t{255});

    VendorClaim<std::uint32_t> registers_per_sm_bytes{};
    VendorClaim<std::uint32_t> smem_per_sm_bytes{};
    // L1 and shared memory come out of one physical array.
    VendorClaim<std::uint32_t> l1_per_sm_bytes{};
    VendorClaim<std::uint32_t> tmem_per_sm_bytes{};

    VendorClaim<std::uint64_t> l2_bytes{};
    VendorClaim<std::uint64_t> hbm_bytes{};
    VendorClaim<std::uint64_t> hbm_bandwidth_bytes_per_sec{};

    VendorClaim<std::uint16_t> nvlink_lanes{};
    VendorClaim<std::uint64_t> nvlink_bandwidth_bytes_per_sec{};
    VendorClaim<PcieGen> pcie_gen{};
    VendorClaim<std::uint8_t> pcie_lanes{};

    CalibratedValue<float> tflops_fp64{};
    CalibratedValue<float> tflops_fp32{};
    CalibratedValue<float> tflops_tf32{};
    CalibratedValue<float> tflops_fp16{};
    CalibratedValue<float> tflops_bf16{};
    CalibratedValue<float> tflops_fp8{};
    CalibratedValue<float> tflops_fp4{};
    CalibratedValue<float> tops_int8{};

    VendorClaim<std::uint16_t> tdp_watts{};
    VendorClaim<std::uint16_t> thermal_throttle_celsius{};

    // Streaming-multiprocessor version as the vendor numbers it: 90 for
    // Hopper, 100 for Blackwell.
    VendorClaim<std::uint16_t> sm_version{};

    ::fixy::Bits<GpuFeature> features{};
};

struct NicPortTargetCaps {
    VendorClaim<LinkLayer> link_layer{};
    VendorClaim<std::uint64_t> line_rate_bytes_per_sec{};
    ValidMtu mtu_bytes = ::fixy::mint_refined<valid_mtu>(std::uint16_t{1500});

    VendorClaim<std::uint16_t> max_tx_queues{};
    VendorClaim<std::uint16_t> max_rx_queues{};
    VendorClaim<std::uint32_t> max_qp_count{};  // RDMA queue pairs
    VendorClaim<std::uint32_t> max_cq_count{};  // RDMA completion queues
    VendorClaim<std::uint32_t> max_mr_count{};  // RDMA memory regions
    VendorClaim<std::uint64_t> max_mr_size_bytes{};
    VendorClaim<std::uint32_t> tcam_entries{};

    // Line rate that survives PCIe, driver and kernel overhead.
    CalibratedValue<std::uint64_t> effective_bandwidth_bytes_per_sec{};
    // Ceiling the socket buffer limits impose, derived from
    // net.core.rmem_max.
    CalibratedValue<std::uint64_t> sysctl_throughput_ceiling_bytes_per_sec{};
    // Bandwidth-delay-product ceiling at the measured round-trip time.
    CalibratedValue<std::uint64_t> bdp_ceiling_bytes_per_sec{};

    VendorClaim<std::uint16_t> pcie_root_complex_id{};
    VendorClaim<PcieGen> pcie_gen{};
    VendorClaim<std::uint8_t> pcie_lanes{};

    // Peer GPUs this port can reach by direct DMA. The span points into
    // an arena this struct does not own and which must outlive it. An
    // empty span means no peers, or discovery has not run.
    std::span<const CogIdentity> gpu_direct_peers{};

    ::fixy::Bits<NicFeature> features{};
};

struct NvSwitchTargetCaps {
    VendorClaim<std::uint16_t> port_count{};
    VendorClaim<std::uint64_t> per_port_bandwidth_bytes_per_sec{};
    VendorClaim<std::uint64_t> aggregate_bandwidth_bytes_per_sec{};

    VendorClaim<std::uint64_t> buffer_bytes{};
    VendorClaim<std::uint32_t> tcam_entries{};

    CalibratedValue<std::uint64_t> effective_aggregate_bytes_per_sec{};

    ::fixy::Bits<SwitchFeature> features{};
};

struct CpuCoreTargetCaps {
    VendorClaim<std::uint32_t> base_clock_mhz{};
    VendorClaim<std::uint32_t> max_clock_mhz{};
    PowerOfTwoLane simd_vector_lanes = ::fixy::mint_refined<power_of_two_lane>(std::uint16_t{8});

    VendorClaim<std::uint32_t> l1d_bytes{};
    VendorClaim<std::uint32_t> l1i_bytes{};
    VendorClaim<std::uint32_t> l2_bytes{};

    CalibratedValue<float> tflops_fp64{};
    CalibratedValue<float> tflops_fp32{};
    CalibratedValue<float> tflops_bf16{};

    ::fixy::Bits<CpuFeature> features{};
};

struct CpuSocketTargetCaps {
    VendorClaim<std::uint16_t> core_count{};
    VendorClaim<std::uint16_t> thread_count{};

    VendorClaim<std::uint64_t> l3_bytes{};
    VendorClaim<std::uint8_t> numa_node_count = ::fixy::mint_tagged<::fixy::tags::source::Vendor, std::uint8_t>(1);

    CalibratedValue<float> memory_bandwidth_bytes_per_sec_per_socket{};

    VendorClaim<std::uint16_t> tdp_watts{};
    VendorClaim<std::uint16_t> thermal_throttle_celsius{};

    // Every core in the socket shares this description.
    CpuCoreTargetCaps representative_core{};

    ::fixy::Bits<CpuFeature> features{};
};

struct DramChannelTargetCaps {
    // 32, 64 or 128
    VendorClaim<std::uint8_t> channel_width_bits = ::fixy::mint_tagged<::fixy::tags::source::Vendor, std::uint8_t>(64);
    VendorClaim<std::uint16_t> speed_mts{};  // mega-transfers per second

    CalibratedValue<std::uint64_t> bandwidth_bytes_per_sec{};

    VendorClaim<std::uint64_t> capacity_bytes{};

    ::fixy::Bits<DramFeature> features{};
};

// The primary template stays undefined. A kind with no schema then
// fails at the point of use rather than binding to a wrong schema.
template <CogKind K>
struct caps_for;

template <>
struct caps_for<CogKind::Gpu> {
    using type = GpuTargetCaps;
};
template <>
struct caps_for<CogKind::CpuCore> {
    using type = CpuCoreTargetCaps;
};
template <>
struct caps_for<CogKind::CpuSocket> {
    using type = CpuSocketTargetCaps;
};
template <>
struct caps_for<CogKind::NicPort> {
    using type = NicPortTargetCaps;
};
template <>
struct caps_for<CogKind::NvSwitch> {
    using type = NvSwitchTargetCaps;
};
template <>
struct caps_for<CogKind::DramChannel> {
    using type = DramChannelTargetCaps;
};

template <CogKind K>
using caps_for_t = typename caps_for<K>::type;

template <CogKind K>
concept HasCaps = requires { typename caps_for<K>::type; };

namespace detail::target_caps_self_test {

static_assert(sizeof(VendorClaim<std::uint16_t>) == sizeof(std::uint16_t),
              "A provenance tag must add no storage to the value it tags.");
static_assert(sizeof(CalibratedValue<std::uint64_t>) == sizeof(std::uint64_t));
static_assert(sizeof(CalibratedValue<float>) == sizeof(float));

static_assert(sizeof(::fixy::Bits<GpuFeature>) == sizeof(std::uint32_t));
static_assert(sizeof(::fixy::Bits<NicFeature>) == sizeof(std::uint32_t));
static_assert(sizeof(::fixy::Bits<SwitchFeature>) == sizeof(std::uint16_t));
static_assert(sizeof(::fixy::Bits<CpuFeature>) == sizeof(std::uint32_t));
static_assert(sizeof(::fixy::Bits<DramFeature>) == sizeof(std::uint8_t));

// The defaults that are not zero are built through the checked doors, so
// a schema default that breaks its own refinement stops the build here.
static_assert(GpuTargetCaps{}.warp_size.value() == 32 && GpuTargetCaps{}.max_regs_per_thread.value() == 255);
static_assert(NicPortTargetCaps{}.mtu_bytes.value() == 1500);
static_assert(CpuCoreTargetCaps{}.simd_vector_lanes.value() == 8);
static_assert(CpuSocketTargetCaps{}.numa_node_count.value() == 1);
static_assert(DramChannelTargetCaps{}.channel_width_bits.value() == 64);

[[nodiscard]] consteval bool every_link_layer_has_name() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^LinkLayer));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (link_layer_name([:en:]) == std::string_view{"<unknown LinkLayer>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_link_layer_has_name(), "link_layer_name() switch is missing an arm for at least one "
                                           "LinkLayer atom.");

[[nodiscard]] consteval bool every_pcie_gen_has_name() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^PcieGen));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (pcie_gen_name([:en:]) == std::string_view{"<unknown PcieGen>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_pcie_gen_has_name());

[[nodiscard]] consteval bool every_gpu_feature_has_name() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^GpuFeature));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (gpu_feature_name([:en:]) == std::string_view{"<unknown GpuFeature>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_gpu_feature_has_name());

[[nodiscard]] consteval bool every_nic_feature_has_name() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^NicFeature));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (nic_feature_name([:en:]) == std::string_view{"<unknown NicFeature>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_nic_feature_has_name());

[[nodiscard]] consteval bool every_switch_feature_has_name() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^SwitchFeature));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (switch_feature_name([:en:]) == std::string_view{"<unknown SwitchFeature>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_switch_feature_has_name());

[[nodiscard]] consteval bool every_cpu_feature_has_name() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^CpuFeature));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (cpu_feature_name([:en:]) == std::string_view{"<unknown CpuFeature>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_cpu_feature_has_name());

[[nodiscard]] consteval bool every_dram_feature_has_name() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^DramFeature));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (dram_feature_name([:en:]) == std::string_view{"<unknown DramFeature>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_dram_feature_has_name());

static_assert(static_cast<std::uint8_t>(LinkLayer::Ethernet) == 0,
              "LinkLayer::Ethernet has moved off its frozen value. Every cached "
              "snapshot that carries this atom now decodes to the wrong link layer.");
static_assert(static_cast<std::uint8_t>(LinkLayer::Infiniband) == 1);
static_assert(static_cast<std::uint8_t>(LinkLayer::Roce) == 2);
static_assert(static_cast<std::uint8_t>(LinkLayer::NVLink) == 3);
static_assert(static_cast<std::uint8_t>(LinkLayer::Pcie) == 4);
static_assert(static_cast<std::uint8_t>(LinkLayer::Cxl) == 5);

static_assert(static_cast<std::uint8_t>(PcieGen::None) == 0);
static_assert(static_cast<std::uint8_t>(PcieGen::Gen1) == 1);
static_assert(static_cast<std::uint8_t>(PcieGen::Gen2) == 2);
static_assert(static_cast<std::uint8_t>(PcieGen::Gen3) == 3);
static_assert(static_cast<std::uint8_t>(PcieGen::Gen4) == 4);
static_assert(static_cast<std::uint8_t>(PcieGen::Gen5) == 5);
static_assert(static_cast<std::uint8_t>(PcieGen::Gen6) == 6);

static_assert(static_cast<std::uint32_t>(GpuFeature::Tma) == (1u << 0));
static_assert(static_cast<std::uint32_t>(GpuFeature::ClusterLaunch) == (1u << 1));
static_assert(static_cast<std::uint32_t>(GpuFeature::Fp8) == (1u << 2));
static_assert(static_cast<std::uint32_t>(GpuFeature::Bf16) == (1u << 3));
static_assert(static_cast<std::uint32_t>(GpuFeature::Tf32) == (1u << 4));
static_assert(static_cast<std::uint32_t>(GpuFeature::NvlinkSharp) == (1u << 5));
static_assert(static_cast<std::uint32_t>(GpuFeature::GpuDirectRdma) == (1u << 6));
static_assert(static_cast<std::uint32_t>(GpuFeature::GpuDirectStorage) == (1u << 7));
static_assert(static_cast<std::uint32_t>(GpuFeature::Mig) == (1u << 8));

static_assert(static_cast<std::uint32_t>(NicFeature::Tso) == (1u << 0));
static_assert(static_cast<std::uint32_t>(NicFeature::Gso) == (1u << 1));
static_assert(static_cast<std::uint32_t>(NicFeature::Gro) == (1u << 2));
static_assert(static_cast<std::uint32_t>(NicFeature::Lro) == (1u << 3));
static_assert(static_cast<std::uint32_t>(NicFeature::Rss) == (1u << 4));
static_assert(static_cast<std::uint32_t>(NicFeature::Roce) == (1u << 5));
static_assert(static_cast<std::uint32_t>(NicFeature::Iwarp) == (1u << 6));
static_assert(static_cast<std::uint32_t>(NicFeature::KtlsOffload) == (1u << 7));
static_assert(static_cast<std::uint32_t>(NicFeature::GpuDirectRdma) == (1u << 8));
static_assert(static_cast<std::uint32_t>(NicFeature::XdpNative) == (1u << 9));
static_assert(static_cast<std::uint32_t>(NicFeature::XdpOffload) == (1u << 10));
static_assert(static_cast<std::uint32_t>(NicFeature::AfXdp) == (1u << 11));
static_assert(static_cast<std::uint32_t>(NicFeature::SrIov) == (1u << 12));
static_assert(static_cast<std::uint32_t>(NicFeature::Macsec) == (1u << 13));
static_assert(static_cast<std::uint32_t>(NicFeature::Ipsec) == (1u << 14));
static_assert(static_cast<std::uint32_t>(NicFeature::TimestampingHw) == (1u << 15));
static_assert(static_cast<std::uint32_t>(NicFeature::TcEbpf) == (1u << 16));
static_assert(static_cast<std::uint32_t>(NicFeature::Tcam) == (1u << 17));

static_assert(static_cast<std::uint16_t>(SwitchFeature::Sharp) == (1u << 0));
static_assert(static_cast<std::uint16_t>(SwitchFeature::P4) == (1u << 1));
static_assert(static_cast<std::uint16_t>(SwitchFeature::AdaptiveRouting) == (1u << 2));
static_assert(static_cast<std::uint16_t>(SwitchFeature::Ecn) == (1u << 3));
static_assert(static_cast<std::uint16_t>(SwitchFeature::Pfc) == (1u << 4));
static_assert(static_cast<std::uint16_t>(SwitchFeature::Tcam) == (1u << 5));
static_assert(static_cast<std::uint16_t>(SwitchFeature::PortMirror) == (1u << 6));
static_assert(static_cast<std::uint16_t>(SwitchFeature::Doca) == (1u << 7));

static_assert(static_cast<std::uint32_t>(CpuFeature::Avx2) == (1u << 0));
static_assert(static_cast<std::uint32_t>(CpuFeature::Avx512) == (1u << 1));
static_assert(static_cast<std::uint32_t>(CpuFeature::Amx) == (1u << 2));
static_assert(static_cast<std::uint32_t>(CpuFeature::Vnni) == (1u << 3));
static_assert(static_cast<std::uint32_t>(CpuFeature::Bf16Cpu) == (1u << 4));
static_assert(static_cast<std::uint32_t>(CpuFeature::Fp16Cpu) == (1u << 5));
static_assert(static_cast<std::uint32_t>(CpuFeature::Aes) == (1u << 6));
static_assert(static_cast<std::uint32_t>(CpuFeature::Sha) == (1u << 7));
static_assert(static_cast<std::uint32_t>(CpuFeature::Neon) == (1u << 8));
static_assert(static_cast<std::uint32_t>(CpuFeature::Sve) == (1u << 9));
static_assert(static_cast<std::uint32_t>(CpuFeature::Sve2) == (1u << 10));
static_assert(static_cast<std::uint32_t>(CpuFeature::Sme) == (1u << 11));
static_assert(static_cast<std::uint32_t>(CpuFeature::AmxBf16Arm) == (1u << 12));
static_assert(static_cast<std::uint32_t>(CpuFeature::Mte) == (1u << 13));
static_assert(static_cast<std::uint32_t>(CpuFeature::PauthArm) == (1u << 14));
static_assert(static_cast<std::uint32_t>(CpuFeature::Cet) == (1u << 15));

static_assert(static_cast<std::uint8_t>(DramFeature::Ecc) == (1u << 0));
static_assert(static_cast<std::uint8_t>(DramFeature::OnDieEcc) == (1u << 1));
static_assert(static_cast<std::uint8_t>(DramFeature::PowerDownIdle) == (1u << 2));
static_assert(static_cast<std::uint8_t>(DramFeature::Hbm) == (1u << 3));

// Widening an underlying type re-shapes every schema that holds the
// flags by value, and with it the serialized byte image.
static_assert(std::is_same_v<std::underlying_type_t<LinkLayer>, std::uint8_t>);
static_assert(std::is_same_v<std::underlying_type_t<PcieGen>, std::uint8_t>);
static_assert(std::is_same_v<std::underlying_type_t<GpuFeature>, std::uint32_t>);
static_assert(std::is_same_v<std::underlying_type_t<NicFeature>, std::uint32_t>);
static_assert(std::is_same_v<std::underlying_type_t<SwitchFeature>, std::uint16_t>);
static_assert(std::is_same_v<std::underlying_type_t<CpuFeature>, std::uint32_t>);
static_assert(std::is_same_v<std::underlying_type_t<DramFeature>, std::uint8_t>);

static_assert(std::is_same_v<caps_for_t<CogKind::Gpu>, GpuTargetCaps>);
static_assert(std::is_same_v<caps_for_t<CogKind::CpuCore>, CpuCoreTargetCaps>);
static_assert(std::is_same_v<caps_for_t<CogKind::CpuSocket>, CpuSocketTargetCaps>);
static_assert(std::is_same_v<caps_for_t<CogKind::NicPort>, NicPortTargetCaps>);
static_assert(std::is_same_v<caps_for_t<CogKind::NvSwitch>, NvSwitchTargetCaps>);
static_assert(std::is_same_v<caps_for_t<CogKind::DramChannel>, DramChannelTargetCaps>);

static_assert(HasCaps<CogKind::Gpu>);
static_assert(HasCaps<CogKind::CpuCore>);
static_assert(HasCaps<CogKind::CpuSocket>);
static_assert(HasCaps<CogKind::NicPort>);
static_assert(HasCaps<CogKind::NvSwitch>);
static_assert(HasCaps<CogKind::DramChannel>);
static_assert(!HasCaps<CogKind::PsuRail>);
static_assert(!HasCaps<CogKind::BmcSensor>);
static_assert(!HasCaps<CogKind::Datacenter>);

// A schema is written to disk and shipped over the wire as raw bytes.
// Standard layout is what keeps that byte image stable, so a schema may
// not grow a virtual function, a non-public member or a second base.
static_assert(std::is_standard_layout_v<GpuTargetCaps>);
static_assert(std::is_standard_layout_v<NicPortTargetCaps>);
static_assert(std::is_standard_layout_v<NvSwitchTargetCaps>);
static_assert(std::is_standard_layout_v<CpuCoreTargetCaps>);
static_assert(std::is_standard_layout_v<CpuSocketTargetCaps>);
static_assert(std::is_standard_layout_v<DramChannelTargetCaps>);

}  // namespace detail::target_caps_self_test

}  // namespace crucible::cog
