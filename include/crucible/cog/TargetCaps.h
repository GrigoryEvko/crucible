#pragma once

#include <crucible/cog/CogIdentity.h>
#include <fixy/Bits.h>
#include <fixy/Refined.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>
#include <foundation/reflect/EnumPins.h>

#include <array>
#include <cstdint>
#include <span>
#include <type_traits>

namespace crucible::cog {

// Every enumerator below is frozen by underlying value, and the pin
// tables at the end of this file hold each value. The values describe
// hardware facts that are meant to outlive the process, so a renumber
// would reinterpret a stored schema. New atoms take the next free value
// or bit position and extend their pin table in the same change.
//
// ::foundation::reflect::enum_name gives the name of an enumerator, and
// ::foundation::reflect::enum_count the number of enumerators.
enum class LinkLayer : std::uint8_t {
    Ethernet = 0,
    Infiniband = 1,
    Roce = 2,
    NVLink = 3,
    Pcie = 4,
    Cxl = 5,
};

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

// A figure measured on this host rather than read from the vendor.
template <typename T>
using CalibratedValue = ::fixy::Tagged<T, ::fixy::tags::source::Calibrated>;

// Each refinement below names its predicate, so a site that builds one
// writes ::fixy::mint_refined<predicate>(value) and the check runs there.

// A GPU warp is 32 lanes on NVIDIA and 64 on AMD. A CPU SIMD register
// holds 4 to 64 lanes depending on the ISA.
inline constexpr auto power_of_two_lane =
    ::fixy::all_of<::fixy::power_of_two, ::fixy::bounded_above<std::uint16_t{128}>>;
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

using ::foundation::reflect::enum_pin;
using ::foundation::reflect::pin_enum;

inline constexpr std::array<enum_pin<LinkLayer>, 6> link_layer_pins{
    {{"Ethernet", 0}, {"Infiniband", 1}, {"Roce", 2}, {"NVLink", 3}, {"Pcie", 4}, {"Cxl", 5}}};
static_assert(pin_enum(link_layer_pins), "LinkLayer drifted from link_layer_pins.");

inline constexpr std::array<enum_pin<PcieGen>, 7> pcie_gen_pins{
    {{"None", 0}, {"Gen1", 1}, {"Gen2", 2}, {"Gen3", 3}, {"Gen4", 4}, {"Gen5", 5}, {"Gen6", 6}}};
static_assert(pin_enum(pcie_gen_pins), "PcieGen drifted from pcie_gen_pins.");

inline constexpr std::array<enum_pin<GpuFeature>, 9> gpu_feature_pins{{{"Tma", 1u << 0},
                                                                       {"ClusterLaunch", 1u << 1},
                                                                       {"Fp8", 1u << 2},
                                                                       {"Bf16", 1u << 3},
                                                                       {"Tf32", 1u << 4},
                                                                       {"NvlinkSharp", 1u << 5},
                                                                       {"GpuDirectRdma", 1u << 6},
                                                                       {"GpuDirectStorage", 1u << 7},
                                                                       {"Mig", 1u << 8}}};
static_assert(pin_enum(gpu_feature_pins), "GpuFeature drifted from gpu_feature_pins.");

inline constexpr std::array<enum_pin<NicFeature>, 18> nic_feature_pins{{{"Tso", 1u << 0},
                                                                        {"Gso", 1u << 1},
                                                                        {"Gro", 1u << 2},
                                                                        {"Lro", 1u << 3},
                                                                        {"Rss", 1u << 4},
                                                                        {"Roce", 1u << 5},
                                                                        {"Iwarp", 1u << 6},
                                                                        {"KtlsOffload", 1u << 7},
                                                                        {"GpuDirectRdma", 1u << 8},
                                                                        {"XdpNative", 1u << 9},
                                                                        {"XdpOffload", 1u << 10},
                                                                        {"AfXdp", 1u << 11},
                                                                        {"SrIov", 1u << 12},
                                                                        {"Macsec", 1u << 13},
                                                                        {"Ipsec", 1u << 14},
                                                                        {"TimestampingHw", 1u << 15},
                                                                        {"TcEbpf", 1u << 16},
                                                                        {"Tcam", 1u << 17}}};
static_assert(pin_enum(nic_feature_pins), "NicFeature drifted from nic_feature_pins.");

inline constexpr std::array<enum_pin<SwitchFeature>, 8> switch_feature_pins{{{"Sharp", 1u << 0},
                                                                             {"P4", 1u << 1},
                                                                             {"AdaptiveRouting", 1u << 2},
                                                                             {"Ecn", 1u << 3},
                                                                             {"Pfc", 1u << 4},
                                                                             {"Tcam", 1u << 5},
                                                                             {"PortMirror", 1u << 6},
                                                                             {"Doca", 1u << 7}}};
static_assert(pin_enum(switch_feature_pins), "SwitchFeature drifted from switch_feature_pins.");

inline constexpr std::array<enum_pin<CpuFeature>, 16> cpu_feature_pins{{{"Avx2", 1u << 0},
                                                                        {"Avx512", 1u << 1},
                                                                        {"Amx", 1u << 2},
                                                                        {"Vnni", 1u << 3},
                                                                        {"Bf16Cpu", 1u << 4},
                                                                        {"Fp16Cpu", 1u << 5},
                                                                        {"Aes", 1u << 6},
                                                                        {"Sha", 1u << 7},
                                                                        {"Neon", 1u << 8},
                                                                        {"Sve", 1u << 9},
                                                                        {"Sve2", 1u << 10},
                                                                        {"Sme", 1u << 11},
                                                                        {"AmxBf16Arm", 1u << 12},
                                                                        {"Mte", 1u << 13},
                                                                        {"PauthArm", 1u << 14},
                                                                        {"Cet", 1u << 15}}};
static_assert(pin_enum(cpu_feature_pins), "CpuFeature drifted from cpu_feature_pins.");

inline constexpr std::array<enum_pin<DramFeature>, 4> dram_feature_pins{
    {{"Ecc", 1u << 0}, {"OnDieEcc", 1u << 1}, {"PowerDownIdle", 1u << 2}, {"Hbm", 1u << 3}}};
static_assert(pin_enum(dram_feature_pins), "DramFeature drifted from dram_feature_pins.");

// A pin table holds a value, not a width. Widening an underlying type
// changes the size of every schema that holds the flags by value.
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

// A schema with a refined field is not trivially copyable, because the
// refinement refuses a byte copy, and the NIC schema also holds a span
// into memory it does not own. So no schema travels as raw bytes, and no
// code writes one out. Standard layout keeps offsetof valid on every
// field, so a schema may not grow a virtual function, a non-public
// member or a second base.
static_assert(!std::is_trivially_copyable_v<GpuTargetCaps> && !std::is_trivially_copyable_v<NicPortTargetCaps>);
static_assert(std::is_standard_layout_v<GpuTargetCaps>);
static_assert(std::is_standard_layout_v<NicPortTargetCaps>);
static_assert(std::is_standard_layout_v<NvSwitchTargetCaps>);
static_assert(std::is_standard_layout_v<CpuCoreTargetCaps>);
static_assert(std::is_standard_layout_v<CpuSocketTargetCaps>);
static_assert(std::is_standard_layout_v<DramChannelTargetCaps>);

}  // namespace detail::target_caps_self_test

}  // namespace crucible::cog
