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

}  // namespace crucible::cog
