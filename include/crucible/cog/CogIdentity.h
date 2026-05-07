#pragma once

// ── crucible::cog::CogIdentity — atomic-to-datacenter hardware identity ──
//
// GAPS-185.  Per misc/03_05_2026_networking.md §3.2-§3.3 and CRUCIBLE.md
// L1.  Cog is the atomic addressable hardware unit — every component
// that can independently fail, throttle, or be scheduled to gets a Cog
// identity.  This header defines the identity carrier; downstream
// headers attach capability schemas (GAPS-186 cog/TargetCaps.h),
// calibrated opcode latency tables (GAPS-187 cog/OpcodeLatencyTable.h),
// per-Cog Mimic instances (GAPS-188 mimic/CogMimic.h), and the
// FitsCog<Row, Cog> concept (GAPS-191) that closes the row-typed
// resource budgeting loop on top of effects::ConcurrentRow (GAPS-190).
//
// ── The eight-level hierarchy (FOUND-I04 frozen positions) ──────────
//
//   L0 — Atomic         GPU die, NIC port, CPU core, DRAM channel,
//                       NVMe namespace, NVSwitch, optical transceiver,
//                       PSU rail, PCIe lane group, BMC sensor.
//                       The smallest unit that can independently fail.
//   L1 — Component      GPU package (8x dies), CPU socket (60+ cores),
//                       NIC card (2 ports), NVMe drive (multiple ns),
//                       Rack PSU, PCIe root complex.
//   L2 — Board          PCB-level: motherboard (CPU+DRAM+PCIe),
//                       GPU baseboard (8 GPUs + NVSwitch).
//   L3 — Chassis        DGX-class server: 1U-8U enclosure with
//                       CPUs, GPUs, NICs, NVMe, PSUs.
//   L4 — Rack           42U rack with N chassis + ToR switches.
//   L5 — Row            Multiple racks under shared cooling / PDU.
//   L6 — Hall           Multiple rows; one DC zone.
//   L7 — Datacenter     Top-level operational unit.
//
// Failure isolation, resource budgets, and Mimic backends all operate
// per-Cog.  When an L0 quarantines, its parent's health score drops
// proportionally (1/N for each child); when a parent's score drops
// below threshold, the parent itself enters suspect state and the
// optimizer routes around it preemptively.
//
// ── Content-addressing ──────────────────────────────────────────────
//
// Identity is content-addressed at the level of `(uuid,
// firmware_revision, bios_revision)` — when firmware updates, the
// effective Cog identity for KernelCache lookup changes (because
// compiled kernels may rely on firmware-specific opcode latencies),
// but the Cog UUID stays the same for operational tracking.  The
// `content_hash(CogIdentity const&)` consteval helper produces the
// KernelCache key axis; the bare `uuid` field is the operational
// identifier (audit logs, fleet inventory, quarantine decisions).
//
// ── Append-only Universe extension (FOUND-I04, mirrors Capabilities.h)
//
// **Existing CogLevel and CogKind values are immutable.**  A change to
// any underlying value already shipped is a federation-cache-key drift
// event — every Cipher checkpoint, KernelCache entry, and serialized
// topology snapshot that mentions the affected level/kind silently
// re-keys, invalidating cross-fleet cache reuse.  An attempt to
// renumber is caught by the in-header static_assert pin block at the
// foot of this file (mirrors the discipline in effects/Capabilities.h
// and effects/Resources.h).  A new level or kind atom MUST land at the
// next free underlying value (8, 9, ...) without disturbing the
// existing pin lines below.
//
//   Axiom coverage: TypeSafe — CogLevel and CogKind are scoped enums
//                   with explicit underlying type; conversions go
//                   through std::to_underlying.  NullSafe — parent
//                   pointer is nullable by design (L7 datacenters
//                   have no parent) and every dereference path
//                   requires an explicit `parent != nullptr` check.
//                   DetSafe — content_hash is consteval-eligible and
//                   produces the same uint64_t for the same
//                   (uuid, firmware_revision, bios_revision) triple
//                   on any supported platform.  InitSafe — every
//                   field has NSDMI; default-constructed CogIdentity
//                   is a fully-specified "zero" value (uuid.is_zero()
//                   true) that the type system distinguishes from
//                   real identities.
//   Runtime cost:   identity is a passive carrier; no virtuals, no
//                   heap, no syscalls.  content_hash is a pure
//                   function over POD-equivalent fields.

#include <crucible/Platform.h>
#include <crucible/safety/Tagged.h>

#include <cstddef>
#include <cstdint>
#include <meta>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::cog {

// ── TopologyEdge forward declaration ────────────────────────────────
//
// GAPS-110 (topology/TopologyGraph.h) owns the full TopologyEdge
// definition: per-edge typed property graph (link kind, observed RTT
// p50/p99, observed bandwidth, drop rate, congestion state, peer
// pointer).  CogIdentity references TopologyEdge only via
// std::span<const TopologyEdge>; span over a forward-declared T is
// well-formed because span's storage is just (T*, size_t) — the
// pointer arithmetic and dereference paths require T to be complete,
// but those live behind member function template bodies that are not
// instantiated until a caller dereferences the span.  GAPS-110 will
// supply the definition; until then, callers may only observe the
// span's emptiness or move it through.
struct TopologyEdge;

// ── Uuid: 128-bit content-hashable identifier ───────────────────────
//
// Stable across firmware updates; derived from a fixed-width vendor
// identifier source (PCIe config-space serial + bus-device-function
// path + SMBIOS UUID + MAC address + BMC ID, depending on Cog kind).
// The exact derivation is owned by GAPS-111 (topology/Discovery.h)
// and GAPS-196 (cog/Calibrate.h) — this header only defines the
// passive carrier and the operations that ride on it.
//
// Layout: two uint64_t fields (hi, lo).  The byte ordering is
// content-portable — comparisons and hashing operate on the value
// fields directly without bit_cast through any byte buffer, so
// little-/big-endian platforms produce the same logical equality
// without serialization-format coupling.  Wire-format encoding (the
// cross-process / cross-fleet transport representation) is owned by
// GAPS-138 (canopy/Hlc.h) and the Cipher serializer.
//
// `is_zero()` distinguishes a default-constructed (uninitialized)
// Uuid from a real-but-rarely-zero Uuid.  Callers must not pass
// zero-Uuid CogIdentity values to content_hash — see content_hash's
// precondition below.
struct Uuid {
    std::uint64_t hi = 0;
    std::uint64_t lo = 0;

    constexpr Uuid() noexcept = default;
    constexpr Uuid(std::uint64_t h, std::uint64_t l) noexcept
        : hi{h}, lo{l} {}

    [[nodiscard]] constexpr bool is_zero() const noexcept {
        return hi == 0 && lo == 0;
    }

    auto operator<=>(const Uuid&) const noexcept = default;
};

static_assert(sizeof(Uuid) == 16,
    "Uuid layout drifted from (uint64_t, uint64_t) — alignment / "
    "padding change would break federation Cipher wire format.");
static_assert(std::is_trivially_copyable_v<Uuid>);
static_assert(std::is_standard_layout_v<Uuid>);

// ── CogLevel: hierarchical position L0..L7 ──────────────────────────
//
// Frozen underlying values per FOUND-I04 — see the pin block at the
// foot of this file.  uint8_t underlying type makes Level cheap to
// store inline in CogIdentity (one byte, EBO-eligible adjacent to
// CogKind).
enum class CogLevel : std::uint8_t {
    L0_Atomic     = 0,
    L1_Component  = 1,
    L2_Board      = 2,
    L3_Chassis    = 3,
    L4_Rack       = 4,
    L5_Row        = 5,
    L6_Hall       = 6,
    L7_Datacenter = 7,
};

inline constexpr std::size_t cog_level_count = 8;

[[nodiscard]] constexpr std::string_view
cog_level_name(CogLevel L) noexcept {
    switch (L) {
        case CogLevel::L0_Atomic:     return "L0_Atomic";
        case CogLevel::L1_Component:  return "L1_Component";
        case CogLevel::L2_Board:      return "L2_Board";
        case CogLevel::L3_Chassis:    return "L3_Chassis";
        case CogLevel::L4_Rack:       return "L4_Rack";
        case CogLevel::L5_Row:        return "L5_Row";
        case CogLevel::L6_Hall:       return "L6_Hall";
        case CogLevel::L7_Datacenter: return "L7_Datacenter";
        default:                      return std::string_view{"<unknown CogLevel>"};
    }
}

// ── CogKind: hardware classification ────────────────────────────────
//
// Frozen underlying values per FOUND-I04.  Three groups visually
// separated below: L0 atoms (0..9), L1 aggregates (10..15), L2..L7
// aggregates (16..20).  The explicit numbering makes the pin lines
// at the foot of the file readable as a freeze ledger.
enum class CogKind : std::uint8_t {
    // L0 atoms
    Gpu                = 0,
    NicPort            = 1,
    CpuCore            = 2,
    DramChannel        = 3,
    NvmeNamespace      = 4,
    NvSwitch           = 5,
    OpticalTransceiver = 6,
    PsuRail            = 7,
    PcieLaneGroup      = 8,
    BmcSensor          = 9,
    // L1 aggregates
    GpuPackage         = 10,
    CpuSocket          = 11,
    NicCard            = 12,
    NvmeDrive          = 13,
    RackPsu            = 14,
    PcieRoot           = 15,
    // L2..L7 aggregates
    Server             = 16,
    Rack               = 17,
    Row                = 18,
    Hall               = 19,
    Datacenter         = 20,
};

inline constexpr std::size_t cog_kind_count = 21;

[[nodiscard]] constexpr std::string_view
cog_kind_name(CogKind K) noexcept {
    switch (K) {
        case CogKind::Gpu:                return "Gpu";
        case CogKind::NicPort:            return "NicPort";
        case CogKind::CpuCore:            return "CpuCore";
        case CogKind::DramChannel:        return "DramChannel";
        case CogKind::NvmeNamespace:      return "NvmeNamespace";
        case CogKind::NvSwitch:           return "NvSwitch";
        case CogKind::OpticalTransceiver: return "OpticalTransceiver";
        case CogKind::PsuRail:            return "PsuRail";
        case CogKind::PcieLaneGroup:      return "PcieLaneGroup";
        case CogKind::BmcSensor:          return "BmcSensor";
        case CogKind::GpuPackage:         return "GpuPackage";
        case CogKind::CpuSocket:          return "CpuSocket";
        case CogKind::NicCard:            return "NicCard";
        case CogKind::NvmeDrive:          return "NvmeDrive";
        case CogKind::RackPsu:            return "RackPsu";
        case CogKind::PcieRoot:           return "PcieRoot";
        case CogKind::Server:             return "Server";
        case CogKind::Rack:               return "Rack";
        case CogKind::Row:                return "Row";
        case CogKind::Hall:               return "Hall";
        case CogKind::Datacenter:         return "Datacenter";
        default:                          return std::string_view{"<unknown CogKind>"};
    }
}

// ── CogFamily: role-orthogonal classification ───────────────────────
//
// The 21-atom CogKind universe partitions into 7 functional families
// orthogonal to the L0..L7 hierarchy.  CogFamily classifies each kind
// by what it DOES rather than where in the hierarchy it sits:
//
//   Compute   — executes user-visible compute (kernels, vector ops,
//               tensor-core MMA).  Mimic emits compiled binaries
//               (cubin / HSACO / NEFF / TPU exec / native ELF).
//               L0 atoms: Gpu, CpuCore.  L1 aggregates: GpuPackage,
//               CpuSocket.  Future: FPGA, NPU, TpuCore, NeuronCore.
//   Network   — moves bytes between Cogs.  Mimic emits RDMA verb
//               sequences, AF_XDP packet templates, eBPF programs,
//               SHARP aggregation trees, multicast routing tables.
//               L0 atoms: NicPort, NvSwitch, OpticalTransceiver.
//               L1 aggregates: NicCard.  Future: DPU, P4 switch.
//   Memory    — stores bytes.  Mimic emits prefetch / refresh
//               schedules, DMA descriptor chains, page-fault hints.
//               L0 atoms: DramChannel, NvmeNamespace.  L1 aggregates:
//               NvmeDrive.  Future: CXL.mem, HBM stack as first-class.
//   Bus       — host/device/peer interconnect.  Mimic emits PCIe
//               config-space writes, link-training schedules, lane-
//               bonding maps.  L0 atoms: PcieLaneGroup.  L1 aggregates:
//               PcieRoot.  Future: CXL switch, UCIe link.
//   Power     — electrical power source/distribution.  No Mimic instance
//               (non-schedulable; observability target only).  L0 atoms:
//               PsuRail.  L1 aggregates: RackPsu.
//   Sensor    — observability source.  No Mimic instance.  L0 atoms:
//               BmcSensor.
//   Container — L2+ enclosure (Server / Rack / Row / Hall / Datacenter).
//               No Mimic instance — the contained L0/L1 Cogs each have
//               their own.  Container Cogs aggregate health/membership;
//               their substance lives in their children.
//
// ── Why orthogonal to CogLevel ──────────────────────────────────────
//
// (Level, Family) form a 2D matrix.  Level says "where in the hierarchy
// does this Cog sit?" — an atom (L0), a component (L1), an enclosure
// (L2+).  Family says "what role does this Cog play?".  An L0 GPU die
// (Level=L0, Family=Compute) and an L1 GPU package (Level=L1, Family=
// Compute) share family but differ in level; an L0 GPU die and an L0
// NIC port share level but differ in family.
//
// ── Append-only Universe extension (FOUND-I04) ──────────────────────
//
// Existing (kind → family) mappings are FROZEN.  Renumbering CogFamily
// values OR re-mapping an existing CogKind to a different family is a
// federation-cache-key drift event — every Cipher checkpoint and
// federation-shared archive that mentioned the affected kind silently
// re-keys.  An attempt to renumber is caught by the static_assert pin
// block at the foot of this file (mirrors the discipline for CogLevel
// and CogKind ordinals).  A new family atom (e.g., CogFamily::Quantum
// for future quantum-coprocessor substrates) MUST land at the next
// free underlying value (7, 8, ...) without disturbing existing pins.
//
// ── Concept gates ────────────────────────────────────────────────────
//
// `IsMimicSubstrate<K>` — K's family is one of {Compute, Network,
// Memory, Bus}, i.e., K hosts a per-Cog Mimic instance per §3.7 of
// misc/03_05_2026_networking.md.  This is the broad gate that
// CogMimic<K> (GAPS-188) consumes.  Subset of substrate kinds that
// have caps_for + opcodes_for shipped today: {Gpu, CpuCore, CpuSocket,
// NicPort, NvSwitch, DramChannel}.  IsMimicSubstrate also admits
// kinds that haven't yet shipped caps_for (e.g., PcieLaneGroup,
// NvmeNamespace) — the operational `HasCaps<K> && HasOpcodeTable<K>`
// conjunction in CogMimic's requires-clause filters those out until
// their schemas land.  Defense in depth: intent gate + operational
// gate.
//
// `IsComputeKind<K>` — STRICT SUBSET of IsMimicSubstrate restricted to
// the Compute family.  Used by downstream code that genuinely only
// schedules compute kernels (e.g., MAP-Elites kernel search; partition
// optimizer's compute-tile placement).  NOT the gate for CogMimic —
// CogMimic admits the broader IsMimicSubstrate, since per §3.7 every
// substrate Cog (network / memory / bus included) gets its own Mimic
// instance with its own substrate-specific emit shape.

enum class CogFamily : std::uint8_t {
    Compute   = 0,
    Network   = 1,
    Memory    = 2,
    Bus       = 3,
    Power     = 4,
    Sensor    = 5,
    Container = 6,
};

inline constexpr std::size_t cog_family_count = 7;

[[nodiscard]] constexpr std::string_view
cog_family_name(CogFamily F) noexcept {
    switch (F) {
        case CogFamily::Compute:   return "Compute";
        case CogFamily::Network:   return "Network";
        case CogFamily::Memory:    return "Memory";
        case CogFamily::Bus:       return "Bus";
        case CogFamily::Power:     return "Power";
        case CogFamily::Sensor:    return "Sensor";
        case CogFamily::Container: return "Container";
        default: return std::string_view{"<unknown CogFamily>"};
    }
}

// Primary template — INTENTIONALLY UNDEFINED.  Reaching here means a
// CogKind atom was added without updating its family mapping.  Compile
// error names the offending atom.
template <CogKind K>
struct cog_family_for;

// L0 atoms (CogKind ordinals 0..9)
template <> struct cog_family_for<CogKind::Gpu>                { static constexpr CogFamily value = CogFamily::Compute; };
template <> struct cog_family_for<CogKind::NicPort>            { static constexpr CogFamily value = CogFamily::Network; };
template <> struct cog_family_for<CogKind::CpuCore>            { static constexpr CogFamily value = CogFamily::Compute; };
template <> struct cog_family_for<CogKind::DramChannel>        { static constexpr CogFamily value = CogFamily::Memory;  };
template <> struct cog_family_for<CogKind::NvmeNamespace>      { static constexpr CogFamily value = CogFamily::Memory;  };
template <> struct cog_family_for<CogKind::NvSwitch>           { static constexpr CogFamily value = CogFamily::Network; };
template <> struct cog_family_for<CogKind::OpticalTransceiver> { static constexpr CogFamily value = CogFamily::Network; };
template <> struct cog_family_for<CogKind::PsuRail>            { static constexpr CogFamily value = CogFamily::Power;   };
template <> struct cog_family_for<CogKind::PcieLaneGroup>      { static constexpr CogFamily value = CogFamily::Bus;     };
template <> struct cog_family_for<CogKind::BmcSensor>          { static constexpr CogFamily value = CogFamily::Sensor;  };
// L1 aggregates (CogKind ordinals 10..15)
template <> struct cog_family_for<CogKind::GpuPackage>         { static constexpr CogFamily value = CogFamily::Compute; };
template <> struct cog_family_for<CogKind::CpuSocket>          { static constexpr CogFamily value = CogFamily::Compute; };
template <> struct cog_family_for<CogKind::NicCard>            { static constexpr CogFamily value = CogFamily::Network; };
template <> struct cog_family_for<CogKind::NvmeDrive>          { static constexpr CogFamily value = CogFamily::Memory;  };
template <> struct cog_family_for<CogKind::RackPsu>            { static constexpr CogFamily value = CogFamily::Power;   };
template <> struct cog_family_for<CogKind::PcieRoot>           { static constexpr CogFamily value = CogFamily::Bus;     };
// L2..L7 enclosures (CogKind ordinals 16..20)
template <> struct cog_family_for<CogKind::Server>             { static constexpr CogFamily value = CogFamily::Container; };
template <> struct cog_family_for<CogKind::Rack>               { static constexpr CogFamily value = CogFamily::Container; };
template <> struct cog_family_for<CogKind::Row>                { static constexpr CogFamily value = CogFamily::Container; };
template <> struct cog_family_for<CogKind::Hall>               { static constexpr CogFamily value = CogFamily::Container; };
template <> struct cog_family_for<CogKind::Datacenter>         { static constexpr CogFamily value = CogFamily::Container; };

template <CogKind K>
inline constexpr CogFamily cog_family_v = cog_family_for<K>::value;

// IsMimicSubstrate<K> — K hosts a per-Cog Mimic instance per §3.7.
// Family is one of {Compute, Network, Memory, Bus}.  Power / Sensor /
// Container kinds are non-substrate; CogMimic<K> refuses them at the
// requires-clause.
template <CogKind K>
concept IsMimicSubstrate =
       cog_family_v<K> == CogFamily::Compute
    || cog_family_v<K> == CogFamily::Network
    || cog_family_v<K> == CogFamily::Memory
    || cog_family_v<K> == CogFamily::Bus;

// IsComputeKind<K> — strict subset of IsMimicSubstrate restricted to
// the Compute family.  Used by code that genuinely only schedules
// compute kernels (MAP-Elites kernel search, partition optimizer
// compute-tile placement).  NOT the gate for CogMimic — see the
// CogFamily doc-block above.
template <CogKind K>
concept IsComputeKind = cog_family_v<K> == CogFamily::Compute;

// ── CogIdentity: the carrier ────────────────────────────────────────
//
// Self-referential by way of a raw `const CogIdentity*` parent — value
// type Optional<CogIdentity> would be ill-formed (recursive sizeof);
// nullable raw pointer is the canonical encoding (nullptr means "no
// parent", which is exactly the L7 datacenter case).  Children and
// neighbor edges live in spans backed by the topology arena (GAPS-110
// owns the storage).
//
// Vendor-supplied attributes (model, firmware_revision, bios_revision)
// carry source::Vendor provenance — they crossed the driver/firmware
// boundary and have not been calibrated against measurement.  The
// optimizer must treat them as "claims" rather than "facts" until
// GAPS-196 calibration reconciles them.
//
// Eight axioms:
//   InitSafe: every field NSDMI; default CogIdentity is well-defined
//             zero state (uuid.is_zero(), level == L0, kind == Gpu,
//             empty vendor strings, zero firmware/bios revisions, no
//             parent, empty children, empty neighbors).
//   TypeSafe: kind/level use scoped enums; provenance via Tagged.
//   NullSafe: parent is nullable; pointer-arithmetic-free.
//   MemSafe:  no owned heap; trivially destructible.
//   BorrowSafe: spans are non-owning; lifetime tied to topology arena
//             via review discipline (GAPS-110 will tighten with
//             Borrowed<T, TopologyArena>).
//   ThreadSafe: no atomics; topology graph is read-mostly; mutation
//             paths run inside Canopy delta-application (GAPS-115)
//             which serializes through SWMR session typing.
//   LeakSafe: passive carrier; no resources.
//   DetSafe:  content_hash is platform-stable; uuid comparison is
//             value-based.
struct CogIdentity {
    Uuid     uuid;                                        // operational identifier
    CogLevel level = CogLevel::L0_Atomic;                 // hierarchical position
    CogKind  kind  = CogKind::Gpu;                        // hardware classification

    // Vendor-supplied provenance.  string_view fields are non-owning;
    // the underlying char storage lives in the topology arena.
    safety::Tagged<std::string_view, safety::source::Vendor> vendor{
        std::string_view{}};
    safety::Tagged<std::string_view, safety::source::Vendor> model{
        std::string_view{}};

    // Firmware / BIOS revisions.  Per task description, opaque
    // uint64_t — vendor-defined encoding (semver, build number, hash
    // of the binary, depending on vendor convention).  CogIdentity
    // does not interpret these; KernelCache lookup uses them as part
    // of the identity tuple and considers them opaque-equal.
    safety::Tagged<std::uint64_t, safety::source::Vendor> firmware_revision{0};
    safety::Tagged<std::uint64_t, safety::source::Vendor> bios_revision{0};

    // Parent pointer (nullptr at L7 = root) and children span.  Both
    // reference storage owned by the topology arena (GAPS-110).  The
    // arena guarantees: (1) no cycles, (2) parent->children contains
    // *this iff this->parent == &parent, (3) lifetime spans the
    // entire fleet-running window (Cipher cold-tier promotion drops
    // the topology only on full reseed).
    const CogIdentity*           parent   = nullptr;
    std::span<const CogIdentity> children{};

    // Neighbor edges.  L2 = direct hardware peers (e.g., GPUs on the
    // same NVSwitch); L3 = network peers (e.g., NICs reachable via
    // the same ToR).  Empty until GAPS-110 populates the topology
    // graph.  Forward-declared TopologyEdge type — span's storage is
    // (ptr, size_t), no T-completeness required at field declaration.
    std::span<const TopologyEdge> neighbors_l2{};
    std::span<const TopologyEdge> neighbors_l3{};
};

static_assert(std::is_trivially_destructible_v<CogIdentity>,
    "CogIdentity must be trivially destructible — no heap, no resources.");
static_assert(std::is_standard_layout_v<CogIdentity>,
    "CogIdentity must be standard-layout for Cipher serialization.");

// ── content_hash: the KernelCache key axis ──────────────────────────
//
// Combines (uuid, firmware_revision, bios_revision) via Family-A
// fmix64-style mixing into a single uint64_t.  Used by KernelCache to
// distinguish compiled kernels for the same physical Cog across
// firmware updates: same uuid + new firmware_revision → different
// content_hash → distinct cache slot → recompile.
//
// Soundness gate: uuid.is_zero() is the "unset" sentinel; hashing it
// produces a content_hash dominated by firmware/bios bits which
// silently collides across uninitialized Cogs.  The contract refuses
// the call so the consteval evaluator surfaces the misuse at compile
// time when the call site reaches it (GAPS-191 FitsCog will be one
// such consteval consumer).
//
// The mix function is a self-contained fmix64 (xxHash final mix); we
// inline it rather than depend on Family-A primitives in
// safety/diag/RowHashFold.h to avoid pulling in the wrapper-tag
// machinery from the cog tree.  Bit-equality across platforms is
// guaranteed: input is uint64_t, output is uint64_t, mixing uses only
// xor / shift / multiply with hex constants — no float, no platform
// intrinsic.
[[nodiscard]] constexpr std::uint64_t
content_hash(CogIdentity const& c) noexcept
    pre (!c.uuid.is_zero())
{
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

// ── Self-test block ─────────────────────────────────────────────────
namespace detail::cog_identity_self_test {

// Cardinality.  Held at eight CogLevel atoms and twenty-one CogKind
// atoms — adding atoms requires updating these guards AND the name-
// coverage lambdas below AND the FOUND-I04 pin block at the file foot.
static_assert(cog_level_count == 8,
    "CogLevel cardinality drifted from the L0..L7 baseline — confirm "
    "the new atom is intentional and update the pin block.");
static_assert(cog_kind_count == 21,
    "CogKind cardinality drifted from the 21-atom baseline — confirm "
    "the new atom is intentional and update the pin block.");

// Reflection-driven name coverage — every CogLevel and CogKind atom
// MUST have a non-sentinel name.  Adding an enumerator without
// updating the corresponding cog_*_name() switch fires here at
// header-inclusion time.
[[nodiscard]] consteval bool every_cog_level_has_name() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^CogLevel));
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
static_assert(every_cog_level_has_name(),
    "cog_level_name() switch is missing an arm for at least one "
    "CogLevel atom — add the arm or the new atom leaks the "
    "'<unknown CogLevel>' sentinel into diagnostics.");

[[nodiscard]] consteval bool every_cog_kind_has_name() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^CogKind));
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
static_assert(every_cog_kind_has_name(),
    "cog_kind_name() switch is missing an arm for at least one "
    "CogKind atom — add the arm or the new atom leaks the "
    "'<unknown CogKind>' sentinel into diagnostics.");

// Underlying types pinned — width drift is silently ABI-breaking.
static_assert(std::is_same_v<std::underlying_type_t<CogLevel>, std::uint8_t>,
    "CogLevel underlying type drifted from uint8_t — ABI change.");
static_assert(std::is_same_v<std::underlying_type_t<CogKind>, std::uint8_t>,
    "CogKind underlying type drifted from uint8_t — ABI change.");

// IsComputeKind smoke — exactly four atoms qualify (Compute family).
static_assert( IsComputeKind<CogKind::Gpu>);
static_assert( IsComputeKind<CogKind::CpuCore>);
static_assert( IsComputeKind<CogKind::GpuPackage>);
static_assert( IsComputeKind<CogKind::CpuSocket>);
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

// IsMimicSubstrate smoke — Compute ∪ Network ∪ Memory ∪ Bus admit.
// Power / Sensor / Container kinds REFUSE.  This is the broad gate
// CogMimic<K> (GAPS-188) consumes; the §3.7 networking.md vision puts
// every substrate Cog under per-Cog Mimic ownership, not just compute.
static_assert( IsMimicSubstrate<CogKind::Gpu>);
static_assert( IsMimicSubstrate<CogKind::CpuCore>);
static_assert( IsMimicSubstrate<CogKind::GpuPackage>);
static_assert( IsMimicSubstrate<CogKind::CpuSocket>);
static_assert( IsMimicSubstrate<CogKind::NicPort>);
static_assert( IsMimicSubstrate<CogKind::NvSwitch>);
static_assert( IsMimicSubstrate<CogKind::OpticalTransceiver>);
static_assert( IsMimicSubstrate<CogKind::NicCard>);
static_assert( IsMimicSubstrate<CogKind::DramChannel>);
static_assert( IsMimicSubstrate<CogKind::NvmeNamespace>);
static_assert( IsMimicSubstrate<CogKind::NvmeDrive>);
static_assert( IsMimicSubstrate<CogKind::PcieLaneGroup>);
static_assert( IsMimicSubstrate<CogKind::PcieRoot>);
static_assert(!IsMimicSubstrate<CogKind::PsuRail>);
static_assert(!IsMimicSubstrate<CogKind::RackPsu>);
static_assert(!IsMimicSubstrate<CogKind::BmcSensor>);
static_assert(!IsMimicSubstrate<CogKind::Server>);
static_assert(!IsMimicSubstrate<CogKind::Rack>);
static_assert(!IsMimicSubstrate<CogKind::Row>);
static_assert(!IsMimicSubstrate<CogKind::Hall>);
static_assert(!IsMimicSubstrate<CogKind::Datacenter>);

// CogFamily cardinality + name coverage.
static_assert(cog_family_count == 7,
    "CogFamily cardinality drifted from 7 — confirm the new family is "
    "intentional and update the FOUND-I04 pin block.");

[[nodiscard]] consteval bool every_cog_family_has_name() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^CogFamily));
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
static_assert(every_cog_family_has_name(),
    "cog_family_name() switch is missing an arm for at least one "
    "CogFamily atom.");

// content_hash determinism — same triple → same hash.
static_assert([] {
    CogIdentity a{};
    a.uuid              = Uuid{0xDEADBEEFULL, 0xCAFEBABEULL};
    a.firmware_revision = safety::Tagged<std::uint64_t,
                                         safety::source::Vendor>{0x12345678ULL};
    a.bios_revision     = safety::Tagged<std::uint64_t,
                                         safety::source::Vendor>{0xABCDEF01ULL};

    CogIdentity b = a;
    return content_hash(a) == content_hash(b);
}(), "content_hash diverged for identical (uuid, firmware, bios) — DetSafe.");

// content_hash discriminates firmware drift — same uuid, different
// firmware_revision → different hash (the entire purpose of including
// firmware in the key axis).
static_assert([] {
    CogIdentity a{};
    a.uuid              = Uuid{0xDEADBEEFULL, 0xCAFEBABEULL};
    a.firmware_revision = safety::Tagged<std::uint64_t,
                                         safety::source::Vendor>{1};

    CogIdentity b = a;
    b.firmware_revision = safety::Tagged<std::uint64_t,
                                         safety::source::Vendor>{2};
    return content_hash(a) != content_hash(b);
}(), "content_hash collapsed firmware_revision — KernelCache would "
     "reuse stale kernels across firmware updates.");

// content_hash discriminates uuid drift — different uuid → different
// hash even with identical firmware/bios.
static_assert([] {
    CogIdentity a{};
    a.uuid = Uuid{1, 0};

    CogIdentity b{};
    b.uuid = Uuid{2, 0};
    return content_hash(a) != content_hash(b);
}(), "content_hash collapsed uuid — distinct Cogs would alias in "
     "KernelCache.");

// ── Append-only Universe pin (FOUND-I04) ────────────────────────────
//
// Underlying values are FROZEN.  A change here is a federation-cache
// wire-format break — every Cipher checkpoint, KernelCache entry, and
// gossip-propagated topology delta that mentions the affected level/
// kind silently re-keys.  These assertions fire instantly on any
// drift, naming the offending atom.
//
// A new atom (e.g., a future L8 stratum, or a new L0 atom like
// "OpticalSwitch") MUST land at the next free underlying value (8 for
// CogLevel, 21+ for CogKind) without disturbing the existing pin
// lines below.

// CogLevel pins (8 atoms)
static_assert(static_cast<std::uint8_t>(CogLevel::L0_Atomic)     == 0,
    "CogLevel::L0_Atomic value drifted — federation row_hash invalidated.");
static_assert(static_cast<std::uint8_t>(CogLevel::L1_Component)  == 1,
    "CogLevel::L1_Component value drifted — federation row_hash invalidated.");
static_assert(static_cast<std::uint8_t>(CogLevel::L2_Board)      == 2,
    "CogLevel::L2_Board value drifted — federation row_hash invalidated.");
static_assert(static_cast<std::uint8_t>(CogLevel::L3_Chassis)    == 3,
    "CogLevel::L3_Chassis value drifted — federation row_hash invalidated.");
static_assert(static_cast<std::uint8_t>(CogLevel::L4_Rack)       == 4,
    "CogLevel::L4_Rack value drifted — federation row_hash invalidated.");
static_assert(static_cast<std::uint8_t>(CogLevel::L5_Row)        == 5,
    "CogLevel::L5_Row value drifted — federation row_hash invalidated.");
static_assert(static_cast<std::uint8_t>(CogLevel::L6_Hall)       == 6,
    "CogLevel::L6_Hall value drifted — federation row_hash invalidated.");
static_assert(static_cast<std::uint8_t>(CogLevel::L7_Datacenter) == 7,
    "CogLevel::L7_Datacenter value drifted — federation row_hash invalidated.");

// CogKind pins (21 atoms — L0 atoms 0..9, L1 aggregates 10..15,
// L2..L7 aggregates 16..20)
static_assert(static_cast<std::uint8_t>(CogKind::Gpu)                ==  0);
static_assert(static_cast<std::uint8_t>(CogKind::NicPort)            ==  1);
static_assert(static_cast<std::uint8_t>(CogKind::CpuCore)            ==  2);
static_assert(static_cast<std::uint8_t>(CogKind::DramChannel)        ==  3);
static_assert(static_cast<std::uint8_t>(CogKind::NvmeNamespace)      ==  4);
static_assert(static_cast<std::uint8_t>(CogKind::NvSwitch)           ==  5);
static_assert(static_cast<std::uint8_t>(CogKind::OpticalTransceiver) ==  6);
static_assert(static_cast<std::uint8_t>(CogKind::PsuRail)            ==  7);
static_assert(static_cast<std::uint8_t>(CogKind::PcieLaneGroup)      ==  8);
static_assert(static_cast<std::uint8_t>(CogKind::BmcSensor)          ==  9);
static_assert(static_cast<std::uint8_t>(CogKind::GpuPackage)         == 10);
static_assert(static_cast<std::uint8_t>(CogKind::CpuSocket)          == 11);
static_assert(static_cast<std::uint8_t>(CogKind::NicCard)            == 12);
static_assert(static_cast<std::uint8_t>(CogKind::NvmeDrive)          == 13);
static_assert(static_cast<std::uint8_t>(CogKind::RackPsu)            == 14);
static_assert(static_cast<std::uint8_t>(CogKind::PcieRoot)           == 15);
static_assert(static_cast<std::uint8_t>(CogKind::Server)             == 16);
static_assert(static_cast<std::uint8_t>(CogKind::Rack)               == 17);
static_assert(static_cast<std::uint8_t>(CogKind::Row)                == 18);
static_assert(static_cast<std::uint8_t>(CogKind::Hall)               == 19);
static_assert(static_cast<std::uint8_t>(CogKind::Datacenter)         == 20);

// CogFamily ordinal pins (7 atoms).  Frozen by FOUND-I04 — federation
// row_hash drift is silent if a value changes; downstream consumers
// of cog_family_v<K> would fold to a different bit pattern.
static_assert(static_cast<std::uint8_t>(CogFamily::Compute)   == 0,
    "CogFamily::Compute drifted — federation row_hash invalidated.");
static_assert(static_cast<std::uint8_t>(CogFamily::Network)   == 1);
static_assert(static_cast<std::uint8_t>(CogFamily::Memory)    == 2);
static_assert(static_cast<std::uint8_t>(CogFamily::Bus)       == 3);
static_assert(static_cast<std::uint8_t>(CogFamily::Power)     == 4);
static_assert(static_cast<std::uint8_t>(CogFamily::Sensor)    == 5);
static_assert(static_cast<std::uint8_t>(CogFamily::Container) == 6);

static_assert(std::is_same_v<std::underlying_type_t<CogFamily>, std::uint8_t>,
    "CogFamily underlying type drifted from uint8_t — ABI change.");

// CogKind → CogFamily mapping pins (21 atoms).  Frozen by FOUND-I04.
// A failure here means someone re-assigned an existing kind to a
// different family — federation row_hash silently re-keys; every
// Cipher checkpoint and shared archive that mentions this kind would
// invalidate.  Re-mapping requires a major-version bump, not a silent
// edit.
static_assert(cog_family_v<CogKind::Gpu>                == CogFamily::Compute);
static_assert(cog_family_v<CogKind::NicPort>            == CogFamily::Network);
static_assert(cog_family_v<CogKind::CpuCore>            == CogFamily::Compute);
static_assert(cog_family_v<CogKind::DramChannel>        == CogFamily::Memory);
static_assert(cog_family_v<CogKind::NvmeNamespace>      == CogFamily::Memory);
static_assert(cog_family_v<CogKind::NvSwitch>           == CogFamily::Network);
static_assert(cog_family_v<CogKind::OpticalTransceiver> == CogFamily::Network);
static_assert(cog_family_v<CogKind::PsuRail>            == CogFamily::Power);
static_assert(cog_family_v<CogKind::PcieLaneGroup>      == CogFamily::Bus);
static_assert(cog_family_v<CogKind::BmcSensor>          == CogFamily::Sensor);
static_assert(cog_family_v<CogKind::GpuPackage>         == CogFamily::Compute);
static_assert(cog_family_v<CogKind::CpuSocket>          == CogFamily::Compute);
static_assert(cog_family_v<CogKind::NicCard>            == CogFamily::Network);
static_assert(cog_family_v<CogKind::NvmeDrive>          == CogFamily::Memory);
static_assert(cog_family_v<CogKind::RackPsu>            == CogFamily::Power);
static_assert(cog_family_v<CogKind::PcieRoot>           == CogFamily::Bus);
static_assert(cog_family_v<CogKind::Server>             == CogFamily::Container);
static_assert(cog_family_v<CogKind::Rack>               == CogFamily::Container);
static_assert(cog_family_v<CogKind::Row>                == CogFamily::Container);
static_assert(cog_family_v<CogKind::Hall>               == CogFamily::Container);
static_assert(cog_family_v<CogKind::Datacenter>         == CogFamily::Container);

} // namespace detail::cog_identity_self_test

} // namespace crucible::cog
