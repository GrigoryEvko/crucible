// The compile-time checks of crucible/cog/OpcodeLatencyTable.h.

#include <crucible/cog/OpcodeLatencyTable.h>

namespace crucible::cog {

static_assert(sizeof(LatencyQuantiles) == 3 * sizeof(std::uint32_t),
              "LatencyQuantiles has gained padding or a field. The serialized "
              "byte layout no longer matches what readers expect.");
static_assert(std::is_trivially_copyable_v<LatencyQuantiles>);
static_assert(std::is_standard_layout_v<LatencyQuantiles>);

static_assert(sizeof(OrderedLatencyQuantiles) == sizeof(LatencyQuantiles),
              "The ordering refinement must add no storage to the triple it "
              "refines.");

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
