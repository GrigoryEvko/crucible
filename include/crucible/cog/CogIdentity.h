#pragma once

#include <crucible/Platform.h>
#include <crucible/safety/_Decide.h>
#include <crucible/safety/_Pre.h>
#include <crucible/safety/_Tagged.h>

#include <cstddef>
#include <cstdint>
#include <meta>
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
// This enum, CogKind and CogFamily are all frozen by underlying value.
// A serialized snapshot outlives the process that wrote it, so
// renumbering an atom silently reinterprets every snapshot that
// carries it. A new atom takes the next free value.
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

inline constexpr std::size_t cog_level_count = 8;

[[nodiscard]] constexpr std::string_view cog_level_name(CogLevel L) noexcept {
    switch (L) {
        case CogLevel::L0_Atomic:
            return "L0_Atomic";
        case CogLevel::L1_Component:
            return "L1_Component";
        case CogLevel::L2_Board:
            return "L2_Board";
        case CogLevel::L3_Chassis:
            return "L3_Chassis";
        case CogLevel::L4_Rack:
            return "L4_Rack";
        case CogLevel::L5_Row:
            return "L5_Row";
        case CogLevel::L6_Hall:
            return "L6_Hall";
        case CogLevel::L7_Datacenter:
            return "L7_Datacenter";
        default:
            return std::string_view{"<unknown CogLevel>"};
    }
}

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

inline constexpr std::size_t cog_kind_count = 21;

[[nodiscard]] constexpr std::string_view cog_kind_name(CogKind K) noexcept {
    switch (K) {
        case CogKind::Gpu:
            return "Gpu";
        case CogKind::NicPort:
            return "NicPort";
        case CogKind::CpuCore:
            return "CpuCore";
        case CogKind::DramChannel:
            return "DramChannel";
        case CogKind::NvmeNamespace:
            return "NvmeNamespace";
        case CogKind::NvSwitch:
            return "NvSwitch";
        case CogKind::OpticalTransceiver:
            return "OpticalTransceiver";
        case CogKind::PsuRail:
            return "PsuRail";
        case CogKind::PcieLaneGroup:
            return "PcieLaneGroup";
        case CogKind::BmcSensor:
            return "BmcSensor";
        case CogKind::GpuPackage:
            return "GpuPackage";
        case CogKind::CpuSocket:
            return "CpuSocket";
        case CogKind::NicCard:
            return "NicCard";
        case CogKind::NvmeDrive:
            return "NvmeDrive";
        case CogKind::RackPsu:
            return "RackPsu";
        case CogKind::PcieRoot:
            return "PcieRoot";
        case CogKind::Server:
            return "Server";
        case CogKind::Rack:
            return "Rack";
        case CogKind::Row:
            return "Row";
        case CogKind::Hall:
            return "Hall";
        case CogKind::Datacenter:
            return "Datacenter";
        default:
            return std::string_view{"<unknown CogKind>"};
    }
}

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

inline constexpr std::size_t cog_family_count = 7;

[[nodiscard]] constexpr std::string_view cog_family_name(CogFamily F) noexcept {
    switch (F) {
        case CogFamily::Compute:
            return "Compute";
        case CogFamily::Network:
            return "Network";
        case CogFamily::Memory:
            return "Memory";
        case CogFamily::Bus:
            return "Bus";
        case CogFamily::Power:
            return "Power";
        case CogFamily::Sensor:
            return "Sensor";
        case CogFamily::Container:
            return "Container";
        default:
            return std::string_view{"<unknown CogFamily>"};
    }
}

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

// A vendor-tagged attribute crossed the driver or firmware boundary
// and has not been reconciled against measurement. It is a claim, not
// a fact.
struct CogIdentity {
    Uuid uuid;
    CogLevel level = CogLevel::L0_Atomic;
    CogKind kind = CogKind::Gpu;

    // The character storage behind these views lives in the topology
    // arena, which must outlive this identity.
    safety::Tagged<std::string_view, safety::source::Vendor> vendor{std::string_view{}};
    safety::Tagged<std::string_view, safety::source::Vendor> model{std::string_view{}};

    // Opaque to this file. A vendor encodes a version number, a build
    // number or a hash of the image as it pleases, and the only
    // operation performed on the value is equality.
    safety::Tagged<std::uint64_t, safety::source::Vendor> firmware_revision{0};
    safety::Tagged<std::uint64_t, safety::source::Vendor> bios_revision{0};

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
static_assert(std::is_standard_layout_v<CogIdentity>,
              "CogIdentity must stay standard-layout so it can be serialized as raw bytes.");

// The key a compiled-kernel cache looks up. The same physical Cog on
// new firmware hashes differently, so kernels compiled against the old
// firmware's opcode latencies are not reused.
//
// The mixing step is written out here rather than taken from the
// shared hash primitives, which would pull the wrapper-tag machinery
// into this tree for six lines of arithmetic. It uses only exclusive
// or, shift and multiply on 64-bit values, so the result is identical
// on every platform.
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
    CRUCIBLE_PRE(crucible::decide::is_non_zero(c.uuid));
    constexpr auto fmix = [](std::uint64_t h) constexpr noexcept {
        h ^= h >> 33;
        h *= 0xFF51AFD7ED558CCDULL;
        h ^= h >> 33;
        h *= 0xC4CEB9FE1A85EC53ULL;
        h ^= h >> 33;
        return h;
    };
    std::uint64_t h = c.uuid.hi;
    h = fmix(h ^ c.uuid.lo);
    h = fmix(h ^ c.firmware_revision.value());
    h = fmix(h ^ c.bios_revision.value());
    return h;
}

namespace detail::cog_identity_self_test {

static_assert(cog_level_count == 8, "The level count no longer matches the enum. Confirm the new atom "
                                    "is intended and pin its value below.");
static_assert(cog_kind_count == 21, "The kind count no longer matches the enum. Confirm the new atom "
                                    "is intended and pin its value below.");

[[nodiscard]] consteval bool every_cog_level_has_name() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^CogLevel));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (cog_level_name([:en:]) == std::string_view{"<unknown CogLevel>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_cog_level_has_name(), "cog_level_name() is missing an arm, so one level reports the "
                                          "'<unknown CogLevel>' sentinel in diagnostics.");

[[nodiscard]] consteval bool every_cog_kind_has_name() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^CogKind));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (cog_kind_name([:en:]) == std::string_view{"<unknown CogKind>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_cog_kind_has_name(), "cog_kind_name() is missing an arm, so one kind reports the "
                                         "'<unknown CogKind>' sentinel in diagnostics.");

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

static_assert(cog_family_count == 7, "The family count no longer matches the enum. Confirm the new "
                                     "atom is intended and pin its value below.");

[[nodiscard]] consteval bool every_cog_family_has_name() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^CogFamily));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (cog_family_name([:en:]) == std::string_view{"<unknown CogFamily>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_cog_family_has_name(), "cog_family_name() is missing an arm, so one family reports "
                                           "the '<unknown CogFamily>' sentinel in diagnostics.");

static_assert(
    [] {
        CogIdentity a{};
        a.uuid = Uuid{0xDEADBEEFULL, 0xCAFEBABEULL};
        a.firmware_revision = safety::Tagged<std::uint64_t, safety::source::Vendor>{0x12345678ULL};
        a.bios_revision = safety::Tagged<std::uint64_t, safety::source::Vendor>{0xABCDEF01ULL};

        CogIdentity b = a;
        return content_hash(a) == content_hash(b);
    }(),
    "content_hash returned two different values for two identical identities.");

static_assert(
    [] {
        CogIdentity a{};
        a.uuid = Uuid{0xDEADBEEFULL, 0xCAFEBABEULL};
        a.firmware_revision = safety::Tagged<std::uint64_t, safety::source::Vendor>{1};

        CogIdentity b = a;
        b.firmware_revision = safety::Tagged<std::uint64_t, safety::source::Vendor>{2};
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

static_assert(static_cast<std::uint8_t>(CogLevel::L0_Atomic) == 0,
              "CogLevel::L0_Atomic has moved off its frozen value. Every stored "
              "snapshot that carries a level now decodes to the wrong one.");
static_assert(static_cast<std::uint8_t>(CogLevel::L1_Component) == 1,
              "CogLevel::L1_Component has moved off its frozen value.");
static_assert(static_cast<std::uint8_t>(CogLevel::L2_Board) == 2, "CogLevel::L2_Board has moved off its frozen value.");
static_assert(static_cast<std::uint8_t>(CogLevel::L3_Chassis) == 3,
              "CogLevel::L3_Chassis has moved off its frozen value.");
static_assert(static_cast<std::uint8_t>(CogLevel::L4_Rack) == 4, "CogLevel::L4_Rack has moved off its frozen value.");
static_assert(static_cast<std::uint8_t>(CogLevel::L5_Row) == 5, "CogLevel::L5_Row has moved off its frozen value.");
static_assert(static_cast<std::uint8_t>(CogLevel::L6_Hall) == 6, "CogLevel::L6_Hall has moved off its frozen value.");
static_assert(static_cast<std::uint8_t>(CogLevel::L7_Datacenter) == 7,
              "CogLevel::L7_Datacenter has moved off its frozen value.");

static_assert(static_cast<std::uint8_t>(CogKind::Gpu) == 0);
static_assert(static_cast<std::uint8_t>(CogKind::NicPort) == 1);
static_assert(static_cast<std::uint8_t>(CogKind::CpuCore) == 2);
static_assert(static_cast<std::uint8_t>(CogKind::DramChannel) == 3);
static_assert(static_cast<std::uint8_t>(CogKind::NvmeNamespace) == 4);
static_assert(static_cast<std::uint8_t>(CogKind::NvSwitch) == 5);
static_assert(static_cast<std::uint8_t>(CogKind::OpticalTransceiver) == 6);
static_assert(static_cast<std::uint8_t>(CogKind::PsuRail) == 7);
static_assert(static_cast<std::uint8_t>(CogKind::PcieLaneGroup) == 8);
static_assert(static_cast<std::uint8_t>(CogKind::BmcSensor) == 9);
static_assert(static_cast<std::uint8_t>(CogKind::GpuPackage) == 10);
static_assert(static_cast<std::uint8_t>(CogKind::CpuSocket) == 11);
static_assert(static_cast<std::uint8_t>(CogKind::NicCard) == 12);
static_assert(static_cast<std::uint8_t>(CogKind::NvmeDrive) == 13);
static_assert(static_cast<std::uint8_t>(CogKind::RackPsu) == 14);
static_assert(static_cast<std::uint8_t>(CogKind::PcieRoot) == 15);
static_assert(static_cast<std::uint8_t>(CogKind::Server) == 16);
static_assert(static_cast<std::uint8_t>(CogKind::Rack) == 17);
static_assert(static_cast<std::uint8_t>(CogKind::Row) == 18);
static_assert(static_cast<std::uint8_t>(CogKind::Hall) == 19);
static_assert(static_cast<std::uint8_t>(CogKind::Datacenter) == 20);

static_assert(static_cast<std::uint8_t>(CogFamily::Compute) == 0,
              "CogFamily::Compute has moved off its frozen value. Every stored "
              "snapshot that carries a family now decodes to the wrong one.");
static_assert(static_cast<std::uint8_t>(CogFamily::Network) == 1);
static_assert(static_cast<std::uint8_t>(CogFamily::Memory) == 2);
static_assert(static_cast<std::uint8_t>(CogFamily::Bus) == 3);
static_assert(static_cast<std::uint8_t>(CogFamily::Power) == 4);
static_assert(static_cast<std::uint8_t>(CogFamily::Sensor) == 5);
static_assert(static_cast<std::uint8_t>(CogFamily::Container) == 6);

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
