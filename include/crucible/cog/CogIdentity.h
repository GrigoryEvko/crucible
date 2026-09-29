#pragma once

#include <fixy/Tagged.h>
#include <fixy/Tags.h>
#include <foundation/Platform.h>
#include <foundation/contracts/Decide.h>
#include <foundation/contracts/Pre.h>
#include <foundation/reflect/EnumPins.h>
#include <foundation/reflect/Hash.h>

#include <array>
#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::cog {

// The definition lives elsewhere. A caller that dereferences a
// neighbour span needs it. A caller that only passes the span along or
// asks whether it is empty does not.
struct TopologyEdge;

// A 128-bit hardware identifier, stable across firmware updates. The
// value is derived from whatever fixed vendor identifier the Cog kind
// offers: a PCIe config-space serial, a bus-device-function path, an
// SMBIOS UUID, a MAC address, a management-controller identifier.
//
// Comparison and hashing read the two value fields directly rather
// than reinterpreting a byte buffer, so the result does not depend on
// the host byte order.
//
// A zero value is the sentinel for "not yet discovered".
struct Uuid {
    std::uint64_t hi = 0;
    std::uint64_t lo = 0;

    constexpr Uuid() noexcept = default;
    constexpr Uuid(std::uint64_t h, std::uint64_t l) noexcept : hi{h}, lo{l} {}

    [[nodiscard]] constexpr bool is_zero() const noexcept { return hi == 0 && lo == 0; }

    auto operator<=>(const Uuid&) const noexcept = default;
};

static_assert(sizeof(Uuid) == 16, "Uuid must stay two 64-bit words with no padding. The serialized "
                                  "wire format depends on it.");
static_assert(std::is_trivially_copyable_v<Uuid>);
static_assert(std::is_standard_layout_v<Uuid>);

// Where a Cog sits in the hierarchy. An atomic Cog is the smallest
// unit that can fail, throttle or be scheduled on its own. Each level
// above contains the one below.
//
// This enum, CogKind and CogFamily are all frozen by underlying value,
// and the pin tables at the end of this file hold each value. A stored
// snapshot is meant to outlive the process that wrote it, so a renumber
// would reinterpret a stored atom. A new atom takes the next free value
// and extends its pin table in the same change.
//
// ::foundation::reflect::enum_name gives the name of an enumerator, and
// ::foundation::reflect::enum_count the number of enumerators.
enum class CogLevel : std::uint8_t {
    L0_Atomic = 0,
    L1_Component = 1,
    L2_Board = 2,
    L3_Chassis = 3,
    L4_Rack = 4,
    L5_Row = 5,
    L6_Hall = 6,
    L7_Datacenter = 7,
};

// What a Cog is. The groups below give each kind its level.
enum class CogKind : std::uint8_t {
    // L0 atoms
    Gpu = 0,
    NicPort = 1,
    CpuCore = 2,
    DramChannel = 3,
    NvmeNamespace = 4,
    NvSwitch = 5,
    OpticalTransceiver = 6,
    PsuRail = 7,
    PcieLaneGroup = 8,
    BmcSensor = 9,
    // L1 aggregates
    GpuPackage = 10,
    CpuSocket = 11,
    NicCard = 12,
    NvmeDrive = 13,
    RackPsu = 14,
    PcieRoot = 15,
    // L2..L7 aggregates
    Server = 16,
    Rack = 17,
    Row = 18,
    Hall = 19,
    Datacenter = 20,
};

// What a Cog does, which is independent of where it sits. A GPU die
// and a GPU package share a family and differ in level. A GPU die and
// a NIC port share a level and differ in family.
//
// The mapping from kind to family below is frozen for the same reason
// the ordinals are: a snapshot written under one mapping is read back
// under whatever mapping the reader has.
enum class CogFamily : std::uint8_t {
    Compute = 0,
    Network = 1,
    Memory = 2,
    Bus = 3,
    Power = 4,
    Sensor = 5,
    Container = 6,
};

// The primary template stays undefined, so a kind added without a
// family mapping fails at the point of use and names itself.
template <CogKind K>
struct cog_family_for;

template <>
struct cog_family_for<CogKind::Gpu> {
    static constexpr CogFamily value = CogFamily::Compute;
};
template <>
struct cog_family_for<CogKind::NicPort> {
    static constexpr CogFamily value = CogFamily::Network;
};
template <>
struct cog_family_for<CogKind::CpuCore> {
    static constexpr CogFamily value = CogFamily::Compute;
};
template <>
struct cog_family_for<CogKind::DramChannel> {
    static constexpr CogFamily value = CogFamily::Memory;
};
template <>
struct cog_family_for<CogKind::NvmeNamespace> {
    static constexpr CogFamily value = CogFamily::Memory;
};
template <>
struct cog_family_for<CogKind::NvSwitch> {
    static constexpr CogFamily value = CogFamily::Network;
};
template <>
struct cog_family_for<CogKind::OpticalTransceiver> {
    static constexpr CogFamily value = CogFamily::Network;
};
template <>
struct cog_family_for<CogKind::PsuRail> {
    static constexpr CogFamily value = CogFamily::Power;
};
template <>
struct cog_family_for<CogKind::PcieLaneGroup> {
    static constexpr CogFamily value = CogFamily::Bus;
};
template <>
struct cog_family_for<CogKind::BmcSensor> {
    static constexpr CogFamily value = CogFamily::Sensor;
};
template <>
struct cog_family_for<CogKind::GpuPackage> {
    static constexpr CogFamily value = CogFamily::Compute;
};
template <>
struct cog_family_for<CogKind::CpuSocket> {
    static constexpr CogFamily value = CogFamily::Compute;
};
template <>
struct cog_family_for<CogKind::NicCard> {
    static constexpr CogFamily value = CogFamily::Network;
};
template <>
struct cog_family_for<CogKind::NvmeDrive> {
    static constexpr CogFamily value = CogFamily::Memory;
};
template <>
struct cog_family_for<CogKind::RackPsu> {
    static constexpr CogFamily value = CogFamily::Power;
};
template <>
struct cog_family_for<CogKind::PcieRoot> {
    static constexpr CogFamily value = CogFamily::Bus;
};
template <>
struct cog_family_for<CogKind::Server> {
    static constexpr CogFamily value = CogFamily::Container;
};
template <>
struct cog_family_for<CogKind::Rack> {
    static constexpr CogFamily value = CogFamily::Container;
};
template <>
struct cog_family_for<CogKind::Row> {
    static constexpr CogFamily value = CogFamily::Container;
};
template <>
struct cog_family_for<CogKind::Hall> {
    static constexpr CogFamily value = CogFamily::Container;
};
template <>
struct cog_family_for<CogKind::Datacenter> {
    static constexpr CogFamily value = CogFamily::Container;
};

template <CogKind K>
inline constexpr CogFamily cog_family_v = cog_family_for<K>::value;

// A substrate Cog carries compiled work of its own. The gate states
// intent, and a kind can satisfy it before its capability schema
// exists, so a consumer that needs the schema checks for that as well.
template <CogKind K>
concept IsMimicSubstrate = cog_family_v<K> == CogFamily::Compute || cog_family_v<K> == CogFamily::Network
                        || cog_family_v<K> == CogFamily::Memory || cog_family_v<K> == CogFamily::Bus;

// A narrower gate for code that schedules compute work only. A
// substrate of another family satisfies IsMimicSubstrate but not this.
template <CogKind K>
concept IsComputeKind = cog_family_v<K> == CogFamily::Compute;

// A value that crossed the driver or firmware boundary and has not been
// reconciled against measurement. It is a claim, not a fact.
// ::fixy::mint_tagged<::fixy::tags::source::Vendor, T>(value) builds
// one. Name T at the call: an unsigned long long literal deduces a
// different type than std::uint64_t, and the field refuses it.
template <typename T>
using VendorClaim = ::fixy::Tagged<T, ::fixy::tags::source::Vendor>;

struct CogIdentity {
    Uuid uuid;
    CogLevel level = CogLevel::L0_Atomic;
    CogKind kind = CogKind::Gpu;

    // The character storage behind these views lives in the topology
    // arena, which must outlive this identity.
    VendorClaim<std::string_view> vendor{};
    VendorClaim<std::string_view> model{};

    // Opaque to this file. A vendor encodes a version number, a build
    // number or a hash of the image as it pleases, and the only
    // operation performed on the value is equality.
    VendorClaim<std::uint64_t> firmware_revision{};
    VendorClaim<std::uint64_t> bios_revision{};

    // A null parent means this Cog is a root. The topology arena that
    // owns both the parent and the children guarantees that the graph
    // has no cycles, that a parent lists exactly the children which
    // point back at it, and that the storage outlives every identity
    // referring to it.
    const CogIdentity* parent = nullptr;
    std::span<const CogIdentity> children{};

    // Peers one hardware hop away, such as two GPUs on one switch, and
    // peers one network hop away, such as two NICs behind one
    // top-of-rack switch.
    std::span<const TopologyEdge> neighbors_l2{};
    std::span<const TopologyEdge> neighbors_l3{};
};

static_assert(std::is_trivially_destructible_v<CogIdentity>,
              "CogIdentity owns no resource, so it must stay trivially destructible.");
// An identity points at its parent and borrows its children and
// neighbours through spans, so it never travels as raw bytes.
static_assert(std::is_standard_layout_v<CogIdentity>, "CogIdentity must stay standard-layout, so that offsetof "
                                                      "stays valid on every field.");

// The key a compiled-kernel cache looks up. The same physical Cog on
// new firmware hashes differently, so kernels compiled against the old
// firmware's opcode latencies are not reused.
//
// The mixing step is the Murmur3 finalizer. It uses only exclusive or,
// shift and multiply on 64-bit values, so the result is identical on
// every platform.
[[nodiscard]] constexpr std::uint64_t content_hash(CogIdentity const& c) noexcept {
    // A zero identifier is the "not yet discovered" sentinel. Hashing
    // one gives a value driven only by the revision fields, so two
    // undiscovered Cogs would collide. The precondition refuses the
    // call instead.
    //
    // The macro form rather than a contract clause: the compiler
    // silently drops a contract clause on a const-reference parameter
    // during constant evaluation, which would let a rejection test
    // pass. The macro fires during constant evaluation as well.
    CRUCIBLE_PRE(::foundation::decide::is_non_zero(c.uuid));
    std::uint64_t h = c.uuid.hi;
    h = ::foundation::reflect::fmix64(h ^ c.uuid.lo);
    h = ::foundation::reflect::fmix64(h ^ c.firmware_revision.value());
    h = ::foundation::reflect::fmix64(h ^ c.bios_revision.value());
    return h;
}

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
