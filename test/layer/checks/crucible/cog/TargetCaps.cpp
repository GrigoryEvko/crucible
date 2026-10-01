// The compile-time checks of crucible/cog/TargetCaps.h.

#include <crucible/cog/TargetCaps.h>

namespace crucible::cog {

static_assert(sizeof(PowerOfTwoLane) == sizeof(std::uint16_t),
              "PowerOfTwoLane must collapse to the size of the value it refines.");

static_assert(sizeof(ValidRegsPerThread) == sizeof(std::uint16_t));

static_assert(sizeof(ValidUtilization) == sizeof(float));

static_assert(sizeof(ValidMtu) == sizeof(std::uint16_t));

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

inline constexpr std::array<enum_pin<NicFeature>, 17> nic_feature_pins{{{"Tso", 1u << 0},
                                                                        {"Gso", 1u << 1},
                                                                        {"Gro", 1u << 2},
                                                                        {"Lro", 1u << 3},
                                                                        {"Rss", 1u << 4},
                                                                        {"Roce", 1u << 5},
                                                                        {"Iwarp", 1u << 6},
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

inline constexpr std::array<enum_pin<SwitchFeature>, 6> switch_feature_pins{{{"Sharp", 1u << 0},
                                                                             {"AdaptiveRouting", 1u << 2},
                                                                             {"Ecn", 1u << 3},
                                                                             {"Pfc", 1u << 4},
                                                                             {"Tcam", 1u << 5},
                                                                             {"PortMirror", 1u << 6}}};
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
