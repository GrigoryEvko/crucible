// The compile-time checks of crucible/cog/CogIdentity.h.

#include <crucible/cog/CogIdentity.h>

namespace crucible::cog {

static_assert(sizeof(Uuid) == 16, "Uuid must stay two 64-bit words with no padding. The serialized "
                                  "wire format depends on it.");
static_assert(std::is_trivially_copyable_v<Uuid>);
static_assert(std::is_standard_layout_v<Uuid>);

static_assert(std::is_trivially_destructible_v<CogIdentity>,
              "CogIdentity owns no resource, so it must stay trivially destructible.");
// An identity points at its parent and borrows its children and
// neighbours through spans, so it never travels as raw bytes.
static_assert(std::is_standard_layout_v<CogIdentity>, "CogIdentity must stay standard-layout, so that offsetof "
                                                      "stays valid on every field.");

namespace detail::cog_identity_self_test {

static_assert(std::is_same_v<std::underlying_type_t<CogLevel>, std::uint8_t>,
              "CogLevel must stay one byte wide. Widening it changes the layout of every "
              "structure that stores it.");
static_assert(std::is_same_v<std::underlying_type_t<CogKind>, std::uint8_t>,
              "CogKind must stay one byte wide. Widening it changes the layout of every "
              "structure that stores it.");

static_assert(IsComputeKind<CogKind::Gpu>);
static_assert(IsComputeKind<CogKind::CpuCore>);
static_assert(IsComputeKind<CogKind::GpuPackage>);
static_assert(IsComputeKind<CogKind::CpuSocket>);
static_assert(!IsComputeKind<CogKind::NicPort>);
static_assert(!IsComputeKind<CogKind::DramChannel>);
static_assert(!IsComputeKind<CogKind::NvmeNamespace>);
static_assert(!IsComputeKind<CogKind::NvSwitch>);
static_assert(!IsComputeKind<CogKind::OpticalTransceiver>);
static_assert(!IsComputeKind<CogKind::PsuRail>);
static_assert(!IsComputeKind<CogKind::PcieLaneGroup>);
static_assert(!IsComputeKind<CogKind::BmcSensor>);
static_assert(!IsComputeKind<CogKind::NicCard>);
static_assert(!IsComputeKind<CogKind::NvmeDrive>);
static_assert(!IsComputeKind<CogKind::RackPsu>);
static_assert(!IsComputeKind<CogKind::PcieRoot>);
static_assert(!IsComputeKind<CogKind::Server>);
static_assert(!IsComputeKind<CogKind::Rack>);
static_assert(!IsComputeKind<CogKind::Row>);
static_assert(!IsComputeKind<CogKind::Hall>);
static_assert(!IsComputeKind<CogKind::Datacenter>);

static_assert(IsMimicSubstrate<CogKind::Gpu>);
static_assert(IsMimicSubstrate<CogKind::CpuCore>);
static_assert(IsMimicSubstrate<CogKind::GpuPackage>);
static_assert(IsMimicSubstrate<CogKind::CpuSocket>);
static_assert(IsMimicSubstrate<CogKind::NicPort>);
static_assert(IsMimicSubstrate<CogKind::NvSwitch>);
static_assert(IsMimicSubstrate<CogKind::OpticalTransceiver>);
static_assert(IsMimicSubstrate<CogKind::NicCard>);
static_assert(IsMimicSubstrate<CogKind::DramChannel>);
static_assert(IsMimicSubstrate<CogKind::NvmeNamespace>);
static_assert(IsMimicSubstrate<CogKind::NvmeDrive>);
static_assert(IsMimicSubstrate<CogKind::PcieLaneGroup>);
static_assert(IsMimicSubstrate<CogKind::PcieRoot>);
static_assert(!IsMimicSubstrate<CogKind::PsuRail>);
static_assert(!IsMimicSubstrate<CogKind::RackPsu>);
static_assert(!IsMimicSubstrate<CogKind::BmcSensor>);
static_assert(!IsMimicSubstrate<CogKind::Server>);
static_assert(!IsMimicSubstrate<CogKind::Rack>);
static_assert(!IsMimicSubstrate<CogKind::Row>);
static_assert(!IsMimicSubstrate<CogKind::Hall>);
static_assert(!IsMimicSubstrate<CogKind::Datacenter>);

static_assert(
    [] {
        CogIdentity a{};
        a.uuid = Uuid{0xDEADBEEFULL, 0xCAFEBABEULL};
        a.firmware_revision = ::fixy::mint_tagged<::fixy::tags::source::Vendor, std::uint64_t>(0x12345678ULL);
        a.bios_revision = ::fixy::mint_tagged<::fixy::tags::source::Vendor, std::uint64_t>(0xABCDEF01ULL);

        CogIdentity b = a;
        return content_hash(a) == content_hash(b);
    }(),
    "content_hash returned two different values for two identical identities.");

static_assert(
    [] {
        CogIdentity a{};
        a.uuid = Uuid{0xDEADBEEFULL, 0xCAFEBABEULL};
        a.firmware_revision = ::fixy::mint_tagged<::fixy::tags::source::Vendor, std::uint64_t>(1);

        CogIdentity b = a;
        b.firmware_revision = ::fixy::mint_tagged<::fixy::tags::source::Vendor, std::uint64_t>(2);
        return content_hash(a) != content_hash(b);
    }(),
    "content_hash ignores firmware_revision, so a kernel compiled "
    "against the previous firmware would be reused after an update.");

static_assert(
    [] {
        CogIdentity a{};
        a.uuid = Uuid{1, 0};

        CogIdentity b{};
        b.uuid = Uuid{2, 0};
        return content_hash(a) != content_hash(b);
    }(),
    "content_hash ignores the identifier, so two different Cogs share "
    "one cache slot.");

using ::foundation::reflect::enum_pin;
using ::foundation::reflect::pin_enum;

// A new atom fails the pin until its table names it.
inline constexpr std::array<enum_pin<CogLevel>, 8> cog_level_pins{{{"L0_Atomic", 0},
                                                                   {"L1_Component", 1},
                                                                   {"L2_Board", 2},
                                                                   {"L3_Chassis", 3},
                                                                   {"L4_Rack", 4},
                                                                   {"L5_Row", 5},
                                                                   {"L6_Hall", 6},
                                                                   {"L7_Datacenter", 7}}};
static_assert(pin_enum(cog_level_pins), "CogLevel drifted from cog_level_pins.");

inline constexpr std::array<enum_pin<CogKind>, 21> cog_kind_pins{{{"Gpu", 0},
                                                                  {"NicPort", 1},
                                                                  {"CpuCore", 2},
                                                                  {"DramChannel", 3},
                                                                  {"NvmeNamespace", 4},
                                                                  {"NvSwitch", 5},
                                                                  {"OpticalTransceiver", 6},
                                                                  {"PsuRail", 7},
                                                                  {"PcieLaneGroup", 8},
                                                                  {"BmcSensor", 9},
                                                                  {"GpuPackage", 10},
                                                                  {"CpuSocket", 11},
                                                                  {"NicCard", 12},
                                                                  {"NvmeDrive", 13},
                                                                  {"RackPsu", 14},
                                                                  {"PcieRoot", 15},
                                                                  {"Server", 16},
                                                                  {"Rack", 17},
                                                                  {"Row", 18},
                                                                  {"Hall", 19},
                                                                  {"Datacenter", 20}}};
static_assert(pin_enum(cog_kind_pins), "CogKind drifted from cog_kind_pins.");

inline constexpr std::array<enum_pin<CogFamily>, 7> cog_family_pins{
    {{"Compute", 0}, {"Network", 1}, {"Memory", 2}, {"Bus", 3}, {"Power", 4}, {"Sensor", 5}, {"Container", 6}}};
static_assert(pin_enum(cog_family_pins), "CogFamily drifted from cog_family_pins.");

static_assert(std::is_same_v<std::underlying_type_t<CogFamily>, std::uint8_t>,
              "CogFamily must stay one byte wide. Widening it changes the layout of "
              "every structure that stores it.");

// Moving an existing kind to a different family re-keys every stored
// snapshot that mentions it. That is a version break, not an edit.
static_assert(cog_family_v<CogKind::Gpu> == CogFamily::Compute);
static_assert(cog_family_v<CogKind::NicPort> == CogFamily::Network);
static_assert(cog_family_v<CogKind::CpuCore> == CogFamily::Compute);
static_assert(cog_family_v<CogKind::DramChannel> == CogFamily::Memory);
static_assert(cog_family_v<CogKind::NvmeNamespace> == CogFamily::Memory);
static_assert(cog_family_v<CogKind::NvSwitch> == CogFamily::Network);
static_assert(cog_family_v<CogKind::OpticalTransceiver> == CogFamily::Network);
static_assert(cog_family_v<CogKind::PsuRail> == CogFamily::Power);
static_assert(cog_family_v<CogKind::PcieLaneGroup> == CogFamily::Bus);
static_assert(cog_family_v<CogKind::BmcSensor> == CogFamily::Sensor);
static_assert(cog_family_v<CogKind::GpuPackage> == CogFamily::Compute);
static_assert(cog_family_v<CogKind::CpuSocket> == CogFamily::Compute);
static_assert(cog_family_v<CogKind::NicCard> == CogFamily::Network);
static_assert(cog_family_v<CogKind::NvmeDrive> == CogFamily::Memory);
static_assert(cog_family_v<CogKind::RackPsu> == CogFamily::Power);
static_assert(cog_family_v<CogKind::PcieRoot> == CogFamily::Bus);
static_assert(cog_family_v<CogKind::Server> == CogFamily::Container);
static_assert(cog_family_v<CogKind::Rack> == CogFamily::Container);
static_assert(cog_family_v<CogKind::Row> == CogFamily::Container);
static_assert(cog_family_v<CogKind::Hall> == CogFamily::Container);
static_assert(cog_family_v<CogKind::Datacenter> == CogFamily::Container);

}  // namespace detail::cog_identity_self_test

}  // namespace crucible::cog
