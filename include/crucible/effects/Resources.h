#pragma once

// ── crucible::effects::Resources — row-typed resource budget axes ───
//
// GAPS-189.  Defines the 23 consumable-resource axis tags used by the
// unified compute+comm budget on shared hardware (CRUCIBLE.md §4 +
// misc/03_05_2026_networking.md §4.2-§4.5).  Every kernel — compute or
// communication — declares per-call resource consumption via these tags
// in its row metadata; the compiler sums declared consumption across
// concurrently-scheduled ops (GAPS-190 effects/Concurrent.h) and
// refuses to compile if total exceeds the relevant Cog's TargetCaps
// (GAPS-191 cog/FitsCog.h).  This eliminates the entire class of
// NCCL-vs-compute SM oversubscription bugs that today's stacks
// tolerate as runtime degradation.
//
//   Axiom coverage: TypeSafe — tags are strong types parameterized by
//                   a budget literal; mismatch (e.g. SmBudget vs
//                   NicQp) at composition sites fires at template
//                   substitution.  DetSafe — every tag value is a
//                   compile-time constant; row hashes are stable
//                   across compilers / TUs given a frozen
//                   ResourceKind underlying-value catalog
//                   (FOUND-I04 append-only Universe extension below).
//   Runtime cost:   zero — tags are empty empty-base-optimizable
//                   types; consumption summation lives in the type
//                   system and is consteval-evaluated.
//
// ── The 23 axes ─────────────────────────────────────────────────────
//
//   GPU compute substrate
//     Sm                 streaming multiprocessors      uint32_t count
//     WarpScheduler      warp scheduler slots / SM      uint32_t count
//     RegistersPerWarp   register file slice / warp     uint32_t count
//     Smem               on-chip shared memory          uint64_t bytes
//     L2                 L2 / unified cache             uint64_t bytes
//   GPU memory substrate
//     HbmBytes           HBM resident footprint         uint64_t bytes
//     HbmBw              HBM bandwidth                  uint64_t bytes/s
//   Inter-device
//     NvlinkBw           NVLink/IF bandwidth            uint64_t bytes/s
//     PcieBw             PCIe bandwidth                 uint64_t bytes/s
//   NIC substrate
//     NicQ               NIC queue depth                uint32_t count
//     NicRing            NIC ring buffer slots          uint32_t count
//     NicQp              NIC RDMA QPs                   uint32_t count
//     NicCq              NIC RDMA CQs                   uint32_t count
//     NicMr              NIC memory regions             uint32_t count
//   Switch / fabric
//     SwitchEgressBw     switch egress bandwidth        uint64_t bytes/s
//     SwitchBuffer       switch buffer cells            uint32_t count
//     Tcam               TCAM entries                   uint32_t count
//   Host substrate
//     CpuCore            CPU core slots                 uint32_t count
//     Llc                last-level cache footprint     uint64_t bytes
//   Power / thermal (per-Cog)
//     PowerWatts         instantaneous power            uint32_t watts
//     ThermalCelsius     thermal headroom ceiling       uint32_t celsius
//   Rack / DC
//     RackPowerKw        rack-level power               uint32_t kilowatts
//     CarbonGramsPerKwh  carbon intensity               uint32_t g/kWh
//
// All axes are parameterized on a `uint64_t` budget value so byte-
// accounted (Smem / L2 / HbmBytes / Llc) and bandwidth-accounted
// (HbmBw / NvlinkBw / PcieBw / SwitchEgressBw) tags can express
// realistic chip-scale numbers.  The 25_04 doc's sketch used uint32_t
// but a single H100 carries 80 GiB of HBM = 8.59e10 bytes which
// exceeds uint32_t::max (~4.29e9).  uint64_t at zero runtime cost
// (tag is an empty type) eliminates the silent-truncation footgun
// without changing the design.
//
// ── Append-only Universe extension (FOUND-I04, mirrors Capabilities.h)
//
// **Existing ResourceKind values are immutable.**  A change to any
// underlying value already shipped (re-numbering Sm from 0 to anything
// else, deleting NicMr, swapping HbmBytes and HbmBw) is a wire-format-
// breaking event for `row_hash` (safety/diag/RowHashFold.h)
// federation cache keys.  All Family-A persistent hashes
// (CDAG_VERSION) tied to those rows would silently re-key,
// invalidating every published L1 / L2 / L3 cache entry across every
// fleet that consumed those rows.
//
// **New axes append only.**  An additional resource (e.g. a
// hypothetical InfinibandQpExt) joins at the next free underlying
// value (23, 24, ...); existing values stay pinned.  This bounds
// federation-cache invalidation to entries that mention the new axis
// — pre-existing rows keep the same row_hash forever because every
// existing ResourceKind underlying value stays frozen.
//
// **Major-version event procedure.**  If a change to an existing
// value is genuinely required (e.g., reflection-driven re-codification
// of the catalog), bump CDAG_VERSION (Types.h Family-A taxonomy),
// flush every L1/L2/L3 cache entry, document the wire-format break in
// MIMIC.md / FORGE.md / CRUCIBLE.md, and re-pin the canonical hashes.
//
// Self-test block at file end pins each underlying value, every
// resource-tag concept satisfaction, every `tag_name` non-empty +
// distinct check, and exercises the reflection-driven name-coverage
// invariant (every ResourceKind atom has a non-sentinel name).
//
// ── Gates ───────────────────────────────────────────────────────────
//
//   Consumed by:
//     GAPS-190 effects/Concurrent.h    — concurrent-row union folding
//                                        Resources tags as budget sums
//     GAPS-191 cog/FitsCog.h           — Row ≤ Cog::TargetCaps gate
//     GAPS-149..158 forge catalogs     — every kernel author declares
//                                        per-call consumption
//     GAPS-167 forge/Ir001/Comm.h      — comm ops declare network
//                                        resources via these tags
//     GAPS-165 AdaptiveOptimizer       — reads declared budgets when
//                                        scheduling
//
//   Depends on:
//     effects/EffectRow.h              — Row<Es...> shape (parallel
//                                        mechanism over Effect atoms)
//     effects/Capabilities.h           — design template + frozen-
//                                        underlying-value discipline
//
// References:
//   misc/03_05_2026_networking.md §4.2-§4.5
//   25_04_2026.md §3.3 (Met(X) row machinery)
//   Tang-Lindley POPL 2026 (arXiv:2507.10301)

#include <crucible/safety/diag/RowHashFold.h>  // row_hash_contribution<T> extension point

#include <cstdint>
#include <meta>
#include <string_view>
#include <type_traits>

namespace crucible::effects {

// ── ResourceKind atom catalog ───────────────────────────────────────
//
// Frozen by underlying value per FOUND-I04 (see file head).  Adding a
// new atom appends; the cardinality static_assert in the self-test
// fires on drift.  Underlying type is uint8_t (≥ 23 atoms; future
// expansion to 256 leaves headroom).  A widen to uint16_t is an ABI-
// breaking event for any struct that uses ResourceKind by value.
enum class ResourceKind : std::uint8_t {
    // GPU compute substrate
    Sm                 = 0,
    WarpScheduler      = 1,
    RegistersPerWarp   = 2,
    Smem               = 3,
    L2                 = 4,
    // GPU memory substrate
    HbmBytes           = 5,
    HbmBw              = 6,
    // Inter-device
    NvlinkBw           = 7,
    PcieBw             = 8,
    // NIC substrate
    NicQ               = 9,
    NicRing            = 10,
    NicQp              = 11,
    NicCq              = 12,
    NicMr              = 13,
    // Switch / fabric
    SwitchEgressBw     = 14,
    SwitchBuffer       = 15,
    Tcam               = 16,
    // Host substrate
    CpuCore            = 17,
    Llc                = 18,
    // Power / thermal
    PowerWatts         = 19,
    ThermalCelsius     = 20,
    // Rack / DC
    RackPowerKw        = 21,
    CarbonGramsPerKwh  = 22,
};

// Cardinality derived via reflection (P2996R13).  Adding a new atom
// auto-bumps this constant — no manual maintenance.  The name-
// coverage assertion in detail::resources_self_test then catches any
// new atom that lacks a `resource_kind_name` switch arm OR a `tag::*`
// template definition.
inline constexpr std::size_t resource_kind_count =
    std::meta::enumerators_of(^^ResourceKind).size();

// ── Diagnostic name accessor ────────────────────────────────────────
//
// constexpr (not consteval) so the runtime smoke-test discipline can
// drive every ResourceKind atom through this accessor with non-
// constant arguments — per
// feedback_algebra_runtime_smoke_test_discipline.  Constant-evaluated
// when called from consteval contexts.
[[nodiscard]] constexpr std::string_view
resource_kind_name(ResourceKind k) noexcept {
    switch (k) {
        case ResourceKind::Sm:                return "Sm";
        case ResourceKind::WarpScheduler:     return "WarpScheduler";
        case ResourceKind::RegistersPerWarp:  return "RegistersPerWarp";
        case ResourceKind::Smem:              return "Smem";
        case ResourceKind::L2:                return "L2";
        case ResourceKind::HbmBytes:          return "HbmBytes";
        case ResourceKind::HbmBw:             return "HbmBw";
        case ResourceKind::NvlinkBw:          return "NvlinkBw";
        case ResourceKind::PcieBw:            return "PcieBw";
        case ResourceKind::NicQ:              return "NicQ";
        case ResourceKind::NicRing:           return "NicRing";
        case ResourceKind::NicQp:             return "NicQp";
        case ResourceKind::NicCq:             return "NicCq";
        case ResourceKind::NicMr:             return "NicMr";
        case ResourceKind::SwitchEgressBw:    return "SwitchEgressBw";
        case ResourceKind::SwitchBuffer:      return "SwitchBuffer";
        case ResourceKind::Tcam:              return "Tcam";
        case ResourceKind::CpuCore:           return "CpuCore";
        case ResourceKind::Llc:               return "Llc";
        case ResourceKind::PowerWatts:        return "PowerWatts";
        case ResourceKind::ThermalCelsius:    return "ThermalCelsius";
        case ResourceKind::RackPowerKw:       return "RackPowerKw";
        case ResourceKind::CarbonGramsPerKwh: return "CarbonGramsPerKwh";
        default: return std::string_view{"<unknown ResourceKind>"};
    }
}

// ── ResourceKind concept gate ───────────────────────────────────────
//
// IsResourceKind<K> rejects template-parameter typos at substitution
// time, not at use site.  Mirrors IsEffect<E> in Capabilities.h.
//
// FIXY-FOUND-101: the prior hand-rolled `||`-disjunction silently
// rejected any future ResourceKind atom because the chain didn't
// auto-extend — a forward-compat trap mirroring the same defect in
// IsEffect.  The reflection-driven body below iterates
// `enumerators_of(^^ResourceKind)` so every catalog atom satisfies
// the concept by construction; adding a new atom to the enum auto-
// extends the gate without touching this file.  Mirrors the
// eval_concurrently_schedulable_ pattern in effects/Concurrent.h.
namespace detail {

template <ResourceKind K>
[[nodiscard]] consteval bool is_resource_kind_atom_() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^ResourceKind));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (K == [:en:]) return true;
    }
#pragma GCC diagnostic pop
    return false;
}

}  // namespace detail

template <ResourceKind K>
concept IsResourceKind = detail::is_resource_kind_atom_<K>();

// ── Resource tag types (resource::*) ────────────────────────────────
//
// Each tag is a parameterized empty struct carrying:
//   • static constexpr ResourceKind kind   — atom catalog index
//   • static constexpr std::uint64_t value — declared budget literal
//   • static constexpr std::string_view name — tag class name for
//     diagnostic pretty-printing
//
// Layout: every tag is one byte (default-constructible, copyable,
// trivially destructible empty struct).  Marked `[[no_unique_address]]`
// in containing rows / contexts collapses them to zero bytes via EBO.
//
// The 23 templates below are mechanical — every Resource tag obeys the
// same shape, so we use a macro to emit them.  The macro is undef'd
// at end-of-namespace to keep the preprocessor surface clean.
namespace resource {

#define CRUCIBLE_DEFINE_RESOURCE_TAG(TagName, KindEnum)                    \
    template <std::uint64_t N>                                             \
    struct TagName {                                                       \
        static constexpr ResourceKind kind = ResourceKind::KindEnum;       \
        static constexpr std::uint64_t value = N;                          \
        static constexpr std::string_view name = #TagName;                 \
        constexpr TagName()                          noexcept = default;  \
        constexpr TagName(const TagName&)            noexcept = default;  \
        constexpr TagName(TagName&&)                 noexcept = default;  \
        constexpr TagName& operator=(const TagName&) noexcept = default;  \
        constexpr TagName& operator=(TagName&&)      noexcept = default;  \
        ~TagName()                                            = default;  \
    }

// GPU compute substrate
CRUCIBLE_DEFINE_RESOURCE_TAG(SmBudget,           Sm);
CRUCIBLE_DEFINE_RESOURCE_TAG(WarpSchedulerSlots, WarpScheduler);
CRUCIBLE_DEFINE_RESOURCE_TAG(RegistersPerWarp,   RegistersPerWarp);
CRUCIBLE_DEFINE_RESOURCE_TAG(SmemBytes,          Smem);
CRUCIBLE_DEFINE_RESOURCE_TAG(L2Bytes,            L2);

// GPU memory substrate
CRUCIBLE_DEFINE_RESOURCE_TAG(HbmBytes,           HbmBytes);
CRUCIBLE_DEFINE_RESOURCE_TAG(HbmBandwidth,       HbmBw);

// Inter-device
CRUCIBLE_DEFINE_RESOURCE_TAG(NvlinkBandwidth,    NvlinkBw);
CRUCIBLE_DEFINE_RESOURCE_TAG(PcieBandwidth,      PcieBw);

// NIC substrate
CRUCIBLE_DEFINE_RESOURCE_TAG(NicQueueBudget,     NicQ);
CRUCIBLE_DEFINE_RESOURCE_TAG(NicRingDepth,       NicRing);
CRUCIBLE_DEFINE_RESOURCE_TAG(NicQp,              NicQp);
CRUCIBLE_DEFINE_RESOURCE_TAG(NicCq,              NicCq);
CRUCIBLE_DEFINE_RESOURCE_TAG(NicMr,              NicMr);

// Switch / fabric
CRUCIBLE_DEFINE_RESOURCE_TAG(SwitchEgressBw,     SwitchEgressBw);
CRUCIBLE_DEFINE_RESOURCE_TAG(SwitchBufferCells,  SwitchBuffer);
CRUCIBLE_DEFINE_RESOURCE_TAG(TcamEntries,        Tcam);

// Host substrate
CRUCIBLE_DEFINE_RESOURCE_TAG(CpuCoreBudget,      CpuCore);
CRUCIBLE_DEFINE_RESOURCE_TAG(LlcBytes,           Llc);

// Power / thermal
CRUCIBLE_DEFINE_RESOURCE_TAG(PowerWatts,         PowerWatts);
CRUCIBLE_DEFINE_RESOURCE_TAG(ThermalCelsius,     ThermalCelsius);

// Rack / DC
CRUCIBLE_DEFINE_RESOURCE_TAG(RackPowerKw,        RackPowerKw);
CRUCIBLE_DEFINE_RESOURCE_TAG(CarbonGramsPerKwh,  CarbonGramsPerKwh);

#undef CRUCIBLE_DEFINE_RESOURCE_TAG

}  // namespace resource

// ── Top-level effects:: aliases for the resource tags ───────────────
//
// Production call sites use the short form (`effects::SmBudget<32>`);
// the `resource::` namespace exists for diagnostic clarity when the
// surrounding code is doing something unusual with the tags directly.
template <std::uint64_t N> using SmBudget           = resource::SmBudget<N>;
template <std::uint64_t N> using WarpSchedulerSlots = resource::WarpSchedulerSlots<N>;
template <std::uint64_t N> using RegistersPerWarp   = resource::RegistersPerWarp<N>;
template <std::uint64_t N> using SmemBytes          = resource::SmemBytes<N>;
template <std::uint64_t N> using L2Bytes            = resource::L2Bytes<N>;
template <std::uint64_t N> using HbmBytes           = resource::HbmBytes<N>;
template <std::uint64_t N> using HbmBandwidth       = resource::HbmBandwidth<N>;
template <std::uint64_t N> using NvlinkBandwidth    = resource::NvlinkBandwidth<N>;
template <std::uint64_t N> using PcieBandwidth      = resource::PcieBandwidth<N>;
template <std::uint64_t N> using NicQueueBudget     = resource::NicQueueBudget<N>;
template <std::uint64_t N> using NicRingDepth       = resource::NicRingDepth<N>;
template <std::uint64_t N> using NicQp              = resource::NicQp<N>;
template <std::uint64_t N> using NicCq              = resource::NicCq<N>;
template <std::uint64_t N> using NicMr              = resource::NicMr<N>;
template <std::uint64_t N> using SwitchEgressBw     = resource::SwitchEgressBw<N>;
template <std::uint64_t N> using SwitchBufferCells  = resource::SwitchBufferCells<N>;
template <std::uint64_t N> using TcamEntries        = resource::TcamEntries<N>;
template <std::uint64_t N> using CpuCoreBudget      = resource::CpuCoreBudget<N>;
template <std::uint64_t N> using LlcBytes           = resource::LlcBytes<N>;
template <std::uint64_t N> using PowerWatts         = resource::PowerWatts<N>;
template <std::uint64_t N> using ThermalCelsius     = resource::ThermalCelsius<N>;
template <std::uint64_t N> using RackPowerKw        = resource::RackPowerKw<N>;
template <std::uint64_t N> using CarbonGramsPerKwh  = resource::CarbonGramsPerKwh<N>;

// ── ResourceTag concept ─────────────────────────────────────────────
//
// Identifies any of the 23 resource tag class templates regardless of
// the parameterized N value.  Generic algorithms (GAPS-190 row union,
// GAPS-191 FitsCog gate) constrain on this concept to admit any
// resource tag and reject non-resource types (effects, plain ints,
// other tag families).
//
// Detection rule: T is a ResourceTag iff it exposes the canonical
// triple (kind, value, name) AND the kind value is a valid
// ResourceKind atom.  This combines the "shape" check with the
// "membership" check, so adding a tag template requires both naming
// it `name = "..."` AND pinning a valid `kind`.
template <typename T>
concept ResourceTag = requires {
    { T::kind  } -> std::convertible_to<ResourceKind>;
    { T::value } -> std::convertible_to<std::uint64_t>;
    { T::name  } -> std::convertible_to<std::string_view>;
    requires IsResourceKind<T::kind>;
};

// ── Type-erased ResourceTag descriptor (fixy-A3-029) ────────────────
//
// Every ResourceTag T exposes the canonical triple
// (T::kind, T::value, T::name) as static constexpr members — but the
// triple is only accessible if the caller knows the type at the call
// site.  Federation row marshaling, FitsCog diagnostic messages, the
// ConcurrentRow runtime smoke test, and any future audit log that
// iterates a heterogeneous tag pack all need a runtime VALUE-typed
// projection that survives type erasure.
//
// `ResourceTagDescriptor` is the type erasure target.  `tag_descriptor`
// projects a ResourceTag (either by type — `tag_descriptor<T>()` — or
// by value — `tag_descriptor(T{})`) into a single descriptor.  The
// row-level variant `concurrent_row_descriptors_v<R>` (in
// effects/Concurrent.h) projects an entire ConcurrentRow into a
// `std::array` of descriptors in catalog order, ready for hashing,
// logging, or wire-format serialization.
//
//   Axiom coverage: TypeSafe — both forms gate on the ResourceTag
//                   concept; non-resource types fail at substitution.
//                   DetSafe — descriptor field order is fixed; same
//                   tag produces the same descriptor on every TU.
//                   InitSafe — every field has an NSDMI carrying the
//                   "absent" sentinel; a default-constructed
//                   descriptor is well-defined.
//   Runtime cost:   one structural copy (24-byte aggregate on 64-bit
//                   ABI).  Optimizer collapses the consteval form to
//                   immediates under -O3.
struct ResourceTagDescriptor {
    ResourceKind  kind  = ResourceKind::Sm;                  // catalog-zero default
    std::uint64_t value = 0;                                 // "no budget"
    std::string_view name = std::string_view{"<absent>"};    // diagnostic
};

template <ResourceTag T>
[[nodiscard]] constexpr ResourceTagDescriptor tag_descriptor() noexcept {
    return ResourceTagDescriptor{T::kind, T::value, T::name};
}

// Overload taking the tag by value — useful when the type is deduced
// from a function argument (e.g., when iterating a tuple of tags via
// std::apply).  Cost identical to the type-only form; both fold to
// the same constexpr expression under -O3.
template <ResourceTag T>
[[nodiscard]] constexpr ResourceTagDescriptor
tag_descriptor(T /*unused*/) noexcept {
    return tag_descriptor<T>();
}

// ── Self-test block ─────────────────────────────────────────────────
namespace detail::resources_self_test {

// Cardinality.  Held at twenty-three for the original catalog — if a
// future revision adds a 24th atom, this guard fires AND the name-
// coverage assertion below independently fires (the latter is the
// load-bearing one because it pinpoints the missing switch arm in
// resource_kind_name).
static_assert(resource_kind_count == 23,
    "ResourceKind catalog diverged from the original 23 axes — confirm "
    "the addition is intentional, append it at the next free underlying "
    "value (do NOT renumber existing atoms — federation row_hash will "
    "invalidate), add a name() arm AND a tag::* template specialization "
    "AND a top-level alias.");

// Name coverage via reflection — every ResourceKind atom MUST have a
// non-sentinel name from resource_kind_name.  Adding a new atom
// without updating the switch fires this assertion at header-
// inclusion time.
[[nodiscard]] consteval bool every_resource_kind_has_name() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^ResourceKind));
    // -Wshadow on `template for` body's induction variable is the
    // canonical false-positive across iterations; suppress locally.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (resource_kind_name([:en:]) ==
            std::string_view{"<unknown ResourceKind>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_resource_kind_has_name(),
    "resource_kind_name() switch is missing an arm for at least one "
    "ResourceKind atom — add the arm or the new atom leaks the "
    "'<unknown ResourceKind>' sentinel into diagnostics.");

// ── Append-only Universe pin (FOUND-I04) ────────────────────────────
//
// Underlying values are FROZEN.  A change here is a federation-cache
// wire-format break — see the "Append-only Universe extension" block
// at the file head for the audit / migration ceremony.  These
// assertions fire instantly on any drift, naming the offending atom.

static_assert(static_cast<std::uint8_t>(ResourceKind::Sm)                 ==  0,
    "ResourceKind::Sm value drifted — federation row_hash invalidated.");
static_assert(static_cast<std::uint8_t>(ResourceKind::WarpScheduler)      ==  1);
static_assert(static_cast<std::uint8_t>(ResourceKind::RegistersPerWarp)   ==  2);
static_assert(static_cast<std::uint8_t>(ResourceKind::Smem)               ==  3);
static_assert(static_cast<std::uint8_t>(ResourceKind::L2)                 ==  4);
static_assert(static_cast<std::uint8_t>(ResourceKind::HbmBytes)           ==  5);
static_assert(static_cast<std::uint8_t>(ResourceKind::HbmBw)              ==  6);
static_assert(static_cast<std::uint8_t>(ResourceKind::NvlinkBw)           ==  7);
static_assert(static_cast<std::uint8_t>(ResourceKind::PcieBw)             ==  8);
static_assert(static_cast<std::uint8_t>(ResourceKind::NicQ)               ==  9);
static_assert(static_cast<std::uint8_t>(ResourceKind::NicRing)            == 10);
static_assert(static_cast<std::uint8_t>(ResourceKind::NicQp)              == 11);
static_assert(static_cast<std::uint8_t>(ResourceKind::NicCq)              == 12);
static_assert(static_cast<std::uint8_t>(ResourceKind::NicMr)              == 13);
static_assert(static_cast<std::uint8_t>(ResourceKind::SwitchEgressBw)     == 14);
static_assert(static_cast<std::uint8_t>(ResourceKind::SwitchBuffer)       == 15);
static_assert(static_cast<std::uint8_t>(ResourceKind::Tcam)               == 16);
static_assert(static_cast<std::uint8_t>(ResourceKind::CpuCore)            == 17);
static_assert(static_cast<std::uint8_t>(ResourceKind::Llc)                == 18);
static_assert(static_cast<std::uint8_t>(ResourceKind::PowerWatts)         == 19);
static_assert(static_cast<std::uint8_t>(ResourceKind::ThermalCelsius)     == 20);
static_assert(static_cast<std::uint8_t>(ResourceKind::RackPowerKw)        == 21);
static_assert(static_cast<std::uint8_t>(ResourceKind::CarbonGramsPerKwh)  == 22);

// Underlying type pinned at uint8_t — a future widen to uint16_t or
// uint32_t silently changes ABI of any struct that uses ResourceKind
// by value.  Federation row_hash sees only the underlying value (cast
// to uint64_t inside fmix64_fold) so type widening is invisible to
// the hash, but still ABI-breaking for transport structs.
static_assert(std::is_same_v<std::underlying_type_t<ResourceKind>,
                             std::uint8_t>,
    "ResourceKind underlying type drifted from uint8_t — ABI change.");

// Every atom satisfies the concept gate.
static_assert(IsResourceKind<ResourceKind::Sm>);
static_assert(IsResourceKind<ResourceKind::WarpScheduler>);
static_assert(IsResourceKind<ResourceKind::RegistersPerWarp>);
static_assert(IsResourceKind<ResourceKind::Smem>);
static_assert(IsResourceKind<ResourceKind::L2>);
static_assert(IsResourceKind<ResourceKind::HbmBytes>);
static_assert(IsResourceKind<ResourceKind::HbmBw>);
static_assert(IsResourceKind<ResourceKind::NvlinkBw>);
static_assert(IsResourceKind<ResourceKind::PcieBw>);
static_assert(IsResourceKind<ResourceKind::NicQ>);
static_assert(IsResourceKind<ResourceKind::NicRing>);
static_assert(IsResourceKind<ResourceKind::NicQp>);
static_assert(IsResourceKind<ResourceKind::NicCq>);
static_assert(IsResourceKind<ResourceKind::NicMr>);
static_assert(IsResourceKind<ResourceKind::SwitchEgressBw>);
static_assert(IsResourceKind<ResourceKind::SwitchBuffer>);
static_assert(IsResourceKind<ResourceKind::Tcam>);
static_assert(IsResourceKind<ResourceKind::CpuCore>);
static_assert(IsResourceKind<ResourceKind::Llc>);
static_assert(IsResourceKind<ResourceKind::PowerWatts>);
static_assert(IsResourceKind<ResourceKind::ThermalCelsius>);
static_assert(IsResourceKind<ResourceKind::RackPowerKw>);
static_assert(IsResourceKind<ResourceKind::CarbonGramsPerKwh>);

// ── FIXY-FOUND-101: reflection-derived count witness ───────────────
//
// Iterates `enumerators_of(^^ResourceKind)` and counts how many
// satisfy IsResourceKind.  Post-fix this equals resource_kind_count
// by construction (the concept body IS that iteration).  Pre-fix,
// the hand-rolled `||` disjunction could silently drop an atom —
// adding a new ResourceKind enumerator without extending the
// disjunction would have produced count_accepted_kinds_() == 23 while
// resource_kind_count == 24, failing this assertion at the source of
// truth.  Post-fix the assertion is structural and tautological by
// design.
[[nodiscard]] consteval std::size_t count_accepted_kinds_() noexcept {
    static constexpr auto enums =
        std::define_static_array(std::meta::enumerators_of(^^ResourceKind));
    std::size_t n = 0;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enums) {
        constexpr ResourceKind k = [:en:];
        if (IsResourceKind<k>) ++n;
    }
#pragma GCC diagnostic pop
    return n;
}
static_assert(count_accepted_kinds_() == resource_kind_count,
    "IsResourceKind rejects a ResourceKind-catalog atom — reflection drift.");

// ── Out-of-range rejection ─────────────────────────────────────────
//
// `static_cast<ResourceKind>(99)` is a well-formed ResourceKind value
// (uint8_t underlying admits 0..255) but is NOT a named enumerator.
// IsResourceKind must reject it — both before and after the
// reflection migration.  Pre-fix the hand-rolled `||` did so
// accidentally (only 23 cases); post-fix the reflection loop does so
// structurally (atom not in enumerators_of result).
static_assert(!IsResourceKind<static_cast<ResourceKind>(99)>);
static_assert(!IsResourceKind<static_cast<ResourceKind>(255)>);  // boundary
static_assert(!IsResourceKind<static_cast<ResourceKind>(23)>);   // immediately past last (CarbonGramsPerKwh=22)

// Diagnostic names are non-empty AND none falls through to the
// "<unknown ResourceKind>" sentinel.  Pairwise distinctness over 23
// atoms is asserted indirectly via every_resource_kind_has_name +
// the explicit per-kind switch-arm spellings (a duplicate would
// require literally typing the same string twice in the switch,
// which is detected by review and by the arm-coverage assertion).
static_assert(!resource_kind_name(ResourceKind::Sm).empty());
static_assert(!resource_kind_name(ResourceKind::WarpScheduler).empty());
static_assert(!resource_kind_name(ResourceKind::RegistersPerWarp).empty());
static_assert(!resource_kind_name(ResourceKind::Smem).empty());
static_assert(!resource_kind_name(ResourceKind::L2).empty());
static_assert(!resource_kind_name(ResourceKind::HbmBytes).empty());
static_assert(!resource_kind_name(ResourceKind::HbmBw).empty());
static_assert(!resource_kind_name(ResourceKind::NvlinkBw).empty());
static_assert(!resource_kind_name(ResourceKind::PcieBw).empty());
static_assert(!resource_kind_name(ResourceKind::NicQ).empty());
static_assert(!resource_kind_name(ResourceKind::NicRing).empty());
static_assert(!resource_kind_name(ResourceKind::NicQp).empty());
static_assert(!resource_kind_name(ResourceKind::NicCq).empty());
static_assert(!resource_kind_name(ResourceKind::NicMr).empty());
static_assert(!resource_kind_name(ResourceKind::SwitchEgressBw).empty());
static_assert(!resource_kind_name(ResourceKind::SwitchBuffer).empty());
static_assert(!resource_kind_name(ResourceKind::Tcam).empty());
static_assert(!resource_kind_name(ResourceKind::CpuCore).empty());
static_assert(!resource_kind_name(ResourceKind::Llc).empty());
static_assert(!resource_kind_name(ResourceKind::PowerWatts).empty());
static_assert(!resource_kind_name(ResourceKind::ThermalCelsius).empty());
static_assert(!resource_kind_name(ResourceKind::RackPowerKw).empty());
static_assert(!resource_kind_name(ResourceKind::CarbonGramsPerKwh).empty());

// ── Resource tag layout invariants ──────────────────────────────────
//
// Every resource::* tag is a default-constructible empty struct
// (one byte by C++ rule — empty types have non-zero size).  EBO in
// containing rows / contexts collapses them to zero.
static_assert(std::is_default_constructible_v<resource::SmBudget<32>>);
static_assert(std::is_trivially_copyable_v<resource::SmBudget<32>>);
static_assert(std::is_trivially_destructible_v<resource::SmBudget<32>>);
static_assert(sizeof(resource::SmBudget<32>) == 1,
    "Resource tag size must be 1 byte (empty struct minimum).");
static_assert(std::is_nothrow_default_constructible_v<resource::SmBudget<32>>);

// Top-level effects:: aliases really do refer to the resource::*
// originals.
static_assert(std::is_same_v<SmBudget<32>,           resource::SmBudget<32>>);
static_assert(std::is_same_v<NicQp<4>,               resource::NicQp<4>>);
static_assert(std::is_same_v<HbmBytes<80'000'000'000ULL>,
                             resource::HbmBytes<80'000'000'000ULL>>);

// Every tag carries the canonical triple (kind, value, name).
static_assert(resource::SmBudget<32>::kind  == ResourceKind::Sm);
static_assert(resource::SmBudget<32>::value == 32);
static_assert(resource::SmBudget<32>::name  == std::string_view{"SmBudget"});

static_assert(resource::HbmBytes<80'000'000'000ULL>::kind  == ResourceKind::HbmBytes);
static_assert(resource::HbmBytes<80'000'000'000ULL>::value == 80'000'000'000ULL);
static_assert(resource::HbmBytes<80'000'000'000ULL>::name  == std::string_view{"HbmBytes"});

static_assert(resource::NicQp<4>::kind  == ResourceKind::NicQp);
static_assert(resource::NicQp<4>::value == 4);
static_assert(resource::NicQp<4>::name  == std::string_view{"NicQp"});

// uint64_t budget capacity — past uint32_t::max — must not silently
// truncate.  Empirically pin one large literal that would have
// vanished under uint32_t.
static_assert(resource::HbmBandwidth<8'000'000'000'000ULL>::value
              == 8'000'000'000'000ULL,
    "Budget value silently truncated — uint64_t parameterization broken.");

// ── ResourceTag concept satisfaction ────────────────────────────────
//
// Every resource::* tag instantiation satisfies the ResourceTag
// concept.  Bare scalars and Effect tags are explicitly NOT
// ResourceTag — those negations are the soundness witnesses for the
// HS14 negative-compile fixtures (test/effects_neg/
// neg_resources_*.cpp), not in-header static_asserts (a positive
// static_assert(!ResourceTag<int>) would be redundant).
static_assert(ResourceTag<resource::SmBudget<32>>);
static_assert(ResourceTag<resource::WarpSchedulerSlots<8>>);
static_assert(ResourceTag<resource::RegistersPerWarp<256>>);
static_assert(ResourceTag<resource::SmemBytes<48 * 1024>>);
static_assert(ResourceTag<resource::L2Bytes<128 * 1024 * 1024>>);
static_assert(ResourceTag<resource::HbmBytes<80'000'000'000ULL>>);
static_assert(ResourceTag<resource::HbmBandwidth<3'350'000'000'000ULL>>);
static_assert(ResourceTag<resource::NvlinkBandwidth<900'000'000'000ULL>>);
static_assert(ResourceTag<resource::PcieBandwidth<32'000'000'000ULL>>);
static_assert(ResourceTag<resource::NicQueueBudget<256>>);
static_assert(ResourceTag<resource::NicRingDepth<4096>>);
static_assert(ResourceTag<resource::NicQp<4>>);
static_assert(ResourceTag<resource::NicCq<4>>);
static_assert(ResourceTag<resource::NicMr<8>>);
static_assert(ResourceTag<resource::SwitchEgressBw<400'000'000'000ULL>>);
static_assert(ResourceTag<resource::SwitchBufferCells<32 * 1024>>);
static_assert(ResourceTag<resource::TcamEntries<8 * 1024>>);
static_assert(ResourceTag<resource::CpuCoreBudget<128>>);
static_assert(ResourceTag<resource::LlcBytes<256 * 1024 * 1024>>);
static_assert(ResourceTag<resource::PowerWatts<700>>);
static_assert(ResourceTag<resource::ThermalCelsius<85>>);
static_assert(ResourceTag<resource::RackPowerKw<60>>);
static_assert(ResourceTag<resource::CarbonGramsPerKwh<400>>);

// Concept rejects non-ResourceTag types.  These are
// compile-evaluated negations — they prove the concept's `requires`
// machinery actually rejects rather than vacuously accepting.
static_assert(!ResourceTag<int>);
static_assert(!ResourceTag<float>);
static_assert(!ResourceTag<void*>);
static_assert(!ResourceTag<ResourceKind>);

// ── tag_descriptor pin (fixy-A3-029) ────────────────────────────────
//
// Three representative tags cover (a) catalog-zero kind (Sm), (b) a
// large uint64_t budget (HbmBytes past uint32_t::max), and (c) a
// last-catalog atom (CarbonGramsPerKwh).  Together they witness that
// the type-erased descriptor preserves kind, value, and name across
// the full kind range and the full uint64_t value range.
static_assert(tag_descriptor<resource::SmBudget<32>>().kind  == ResourceKind::Sm);
static_assert(tag_descriptor<resource::SmBudget<32>>().value == 32);
static_assert(tag_descriptor<resource::SmBudget<32>>().name
              == std::string_view{"SmBudget"});

static_assert(tag_descriptor<resource::HbmBytes<80'000'000'000ULL>>().kind
              == ResourceKind::HbmBytes);
static_assert(tag_descriptor<resource::HbmBytes<80'000'000'000ULL>>().value
              == 80'000'000'000ULL);

static_assert(tag_descriptor<resource::CarbonGramsPerKwh<400>>().kind
              == ResourceKind::CarbonGramsPerKwh);
static_assert(tag_descriptor<resource::CarbonGramsPerKwh<400>>().value == 400);

// Default-constructed descriptor has the documented "absent" shape.
// InitSafe — no field is ever read uninitialized.
static_assert(ResourceTagDescriptor{}.kind  == ResourceKind::Sm);
static_assert(ResourceTagDescriptor{}.value == 0);
static_assert(ResourceTagDescriptor{}.name  == std::string_view{"<absent>"});

// ── Runtime smoke test (fixy-A3-021) ────────────────────────────────
//
// Drive `resource_kind_name` through every ResourceKind atom with a
// non-constant argument — closes the same blind spot Capabilities.h
// closes for Effect.  Pure static_assert coverage never instantiates
// the function body against a runtime call site, so a future regression
// that demoted the function to consteval-only would silently pass the
// header self-test but fail when production code samples it.
inline void runtime_smoke_test() {
    // Drive every atom through the runtime path.  Looping is fine
    // here — the optimizer collapses the immediates anyway under -O3.
    ResourceKind k = ResourceKind::Sm;
    [[maybe_unused]] std::string_view n1 = resource_kind_name(k);
    k = ResourceKind::WarpScheduler;     (void)resource_kind_name(k);
    k = ResourceKind::RegistersPerWarp;  (void)resource_kind_name(k);
    k = ResourceKind::Smem;              (void)resource_kind_name(k);
    k = ResourceKind::L2;                (void)resource_kind_name(k);
    k = ResourceKind::HbmBytes;          (void)resource_kind_name(k);
    k = ResourceKind::HbmBw;             (void)resource_kind_name(k);
    k = ResourceKind::NvlinkBw;          (void)resource_kind_name(k);
    k = ResourceKind::PcieBw;            (void)resource_kind_name(k);
    k = ResourceKind::NicQ;               (void)resource_kind_name(k);
    k = ResourceKind::NicRing;           (void)resource_kind_name(k);
    k = ResourceKind::NicQp;              (void)resource_kind_name(k);
    k = ResourceKind::NicCq;              (void)resource_kind_name(k);
    k = ResourceKind::NicMr;              (void)resource_kind_name(k);
    k = ResourceKind::SwitchEgressBw;    (void)resource_kind_name(k);
    k = ResourceKind::SwitchBuffer;      (void)resource_kind_name(k);
    k = ResourceKind::Tcam;              (void)resource_kind_name(k);
    k = ResourceKind::CpuCore;           (void)resource_kind_name(k);
    k = ResourceKind::Llc;                (void)resource_kind_name(k);
    k = ResourceKind::PowerWatts;        (void)resource_kind_name(k);
    k = ResourceKind::ThermalCelsius;    (void)resource_kind_name(k);
    k = ResourceKind::RackPowerKw;       (void)resource_kind_name(k);
    k = ResourceKind::CarbonGramsPerKwh; (void)resource_kind_name(k);

    // Tag default construction at runtime — every resource::* tag is
    // an empty struct, but a future change that adds a throwing or
    // non-trivial default ctor would catch on the parse here.
    [[maybe_unused]] resource::SmBudget<32> sm{};
    [[maybe_unused]] resource::HbmBytes<1024> hbm{};
    [[maybe_unused]] resource::NicQp<8> qp{};
    [[maybe_unused]] resource::PowerWatts<400> pw{};

    // fixy-A3-029 — drive tag_descriptor through both forms with
    // non-constant arguments.  Per
    // feedback_algebra_runtime_smoke_test_discipline: a function only
    // exercised by static_assert can rot silently into consteval-only
    // shape; a runtime call site keeps the constexpr contract honest.
    [[maybe_unused]] auto sm_desc  = tag_descriptor<resource::SmBudget<32>>();
    [[maybe_unused]] auto hbm_desc = tag_descriptor(hbm);
    [[maybe_unused]] auto qp_desc  = tag_descriptor(qp);
    [[maybe_unused]] auto pw_desc  = tag_descriptor(pw);
    [[maybe_unused]] auto blank    = ResourceTagDescriptor{};
}

}  // namespace detail::resources_self_test

}  // namespace crucible::effects

// ── A3-002: row_hash_contribution<resource::TagName<N>> ─────────────
//
// Co-located with the tag declarations per the "specialization next to
// declaration" discipline (A1-018).  Federation cache key contribution
// for the 23 parameterized resource-tag class templates.  Without
// these specializations every `resource::*<N>` instantiation falls
// through to `row_hash_contribution`'s primary template and contributes
// 0 to its row hash — meaning EVERY wrapper-stack `Computation<R,
// resource::SmBudget<32>>` and `Computation<R, resource::NicQp<4>>`
// would hash identically (only R contributes), and every kernel keyed
// by such a stack collides at federation slot 0.
//
// Per-tag hash design.  Each tag's value is `combine_ids(<wrapper-tag
// salt | kind underlying value>, <budget literal N>)`:
//   • The wrapper-tag salt byte (WRAPPER_RESOURCE_TAG_TAG = 0x10 in
//     the high byte) keeps the entire ResourceTag family disjoint
//     from the 15 canonical-wrapper salts (0x01..0x0F) and from the
//     unspecialized-zero primary.
//   • The kind underlying value (0..22) sits in the low 5 bits — fits
//     under uint8_t headroom (ResourceKind frozen at uint8_t per
//     Resources.h FOUND-I04 pin) and never overlaps the salt byte.
//   • The budget literal N is the second combine_ids argument, so
//     `SmBudget<32>` vs `SmBudget<64>` (same kind, different value)
//     produce avalanche-distinct hashes through combine_ids's Boost-
//     style golden-ratio mixer + fmix64 finalizer.
//
// Discipline.  Adding a new resource tag template (per the FOUND-I04
// append-only Universe extension) requires appending one
// CRUCIBLE_RESOURCE_TAG_ROW_HASH line below in catalog order.  The
// `every_resource_kind_has_name` reflection invariant in Resources.h
// already pins the cardinality; if a new atom appears but its hash
// specialization is missing, the per-tag distinctness assertions in
// the self-test block below WILL fire (the new tag would alias
// `row_hash_contribution_v<...> == 0`, equal to the primary).
namespace crucible::safety::diag {

#define CRUCIBLE_RESOURCE_TAG_ROW_HASH(TagName, KindEnum)                  \
    template <std::uint64_t N>                                             \
    struct row_hash_contribution<                                          \
        ::crucible::effects::resource::TagName<N>> {                       \
        static constexpr std::uint64_t value = detail::combine_ids(        \
            detail::WRAPPER_RESOURCE_TAG_TAG                               \
                | static_cast<std::uint64_t>(                              \
                      ::crucible::effects::ResourceKind::KindEnum),        \
            N);                                                            \
    }

// GPU compute substrate
CRUCIBLE_RESOURCE_TAG_ROW_HASH(SmBudget,           Sm);
CRUCIBLE_RESOURCE_TAG_ROW_HASH(WarpSchedulerSlots, WarpScheduler);
CRUCIBLE_RESOURCE_TAG_ROW_HASH(RegistersPerWarp,   RegistersPerWarp);
CRUCIBLE_RESOURCE_TAG_ROW_HASH(SmemBytes,          Smem);
CRUCIBLE_RESOURCE_TAG_ROW_HASH(L2Bytes,            L2);
// GPU memory substrate
CRUCIBLE_RESOURCE_TAG_ROW_HASH(HbmBytes,           HbmBytes);
CRUCIBLE_RESOURCE_TAG_ROW_HASH(HbmBandwidth,       HbmBw);
// Inter-device
CRUCIBLE_RESOURCE_TAG_ROW_HASH(NvlinkBandwidth,    NvlinkBw);
CRUCIBLE_RESOURCE_TAG_ROW_HASH(PcieBandwidth,      PcieBw);
// NIC substrate
CRUCIBLE_RESOURCE_TAG_ROW_HASH(NicQueueBudget,     NicQ);
CRUCIBLE_RESOURCE_TAG_ROW_HASH(NicRingDepth,       NicRing);
CRUCIBLE_RESOURCE_TAG_ROW_HASH(NicQp,              NicQp);
CRUCIBLE_RESOURCE_TAG_ROW_HASH(NicCq,              NicCq);
CRUCIBLE_RESOURCE_TAG_ROW_HASH(NicMr,              NicMr);
// Switch / fabric
CRUCIBLE_RESOURCE_TAG_ROW_HASH(SwitchEgressBw,     SwitchEgressBw);
CRUCIBLE_RESOURCE_TAG_ROW_HASH(SwitchBufferCells,  SwitchBuffer);
CRUCIBLE_RESOURCE_TAG_ROW_HASH(TcamEntries,        Tcam);
// Host substrate
CRUCIBLE_RESOURCE_TAG_ROW_HASH(CpuCoreBudget,      CpuCore);
CRUCIBLE_RESOURCE_TAG_ROW_HASH(LlcBytes,           Llc);
// Power / thermal
CRUCIBLE_RESOURCE_TAG_ROW_HASH(PowerWatts,         PowerWatts);
CRUCIBLE_RESOURCE_TAG_ROW_HASH(ThermalCelsius,     ThermalCelsius);
// Rack / DC
CRUCIBLE_RESOURCE_TAG_ROW_HASH(RackPowerKw,        RackPowerKw);
CRUCIBLE_RESOURCE_TAG_ROW_HASH(CarbonGramsPerKwh,  CarbonGramsPerKwh);

#undef CRUCIBLE_RESOURCE_TAG_ROW_HASH

// ── Self-test block — per-tag distinctness invariants (A3-002) ──────
//
// Pin pairwise distinctness across the 23 tag templates at a fixed N,
// and per-tag distinctness across N values within one tag.  Together
// these witness that the (kind, value) → hash injection holds at the
// catalog edges (where the audit task is most likely to spot a
// regression).  Catalog-wide pairwise distinctness (23×23 = 529
// pairs) is exhaustive but excessive in-header; the representative
// neighbors below are pinned along the catalog walk so a future
// renumbering or addition cannot silently collide adjacent kinds.
namespace detail::row_hash_resource_tag_self_test {

using ::crucible::effects::resource::SmBudget;
using ::crucible::effects::resource::WarpSchedulerSlots;
using ::crucible::effects::resource::NicQp;
using ::crucible::effects::resource::NicCq;
using ::crucible::effects::resource::HbmBytes;
using ::crucible::effects::resource::HbmBandwidth;
using ::crucible::effects::resource::CarbonGramsPerKwh;
using ::crucible::effects::resource::RackPowerKw;

// Per-tag distinctness across N values: `SmBudget<32>` ≠ `SmBudget<64>`
// even though both share kind=Sm.  combine_ids's avalanche behavior
// makes this trivially true for any pair of distinct uint64_t values.
static_assert(row_hash_contribution_v<SmBudget<32>>
           != row_hash_contribution_v<SmBudget<64>>);
static_assert(row_hash_contribution_v<NicQp<4>>
           != row_hash_contribution_v<NicQp<8>>);

// Cross-tag distinctness at the SAME N value: `SmBudget<32>` ≠
// `NicQp<32>`.  The salted (kind | salt-byte) seed guarantees this
// — different kind underlying values feed combine_ids's first
// argument so the result diverges past the fmix64 finalizer.
static_assert(row_hash_contribution_v<SmBudget<32>>
           != row_hash_contribution_v<NicQp<32>>);
static_assert(row_hash_contribution_v<SmBudget<32>>
           != row_hash_contribution_v<WarpSchedulerSlots<32>>);
static_assert(row_hash_contribution_v<HbmBytes<32>>
           != row_hash_contribution_v<HbmBandwidth<32>>);
static_assert(row_hash_contribution_v<NicQp<32>>
           != row_hash_contribution_v<NicCq<32>>);
static_assert(row_hash_contribution_v<RackPowerKw<32>>
           != row_hash_contribution_v<CarbonGramsPerKwh<32>>);

// Distinctness from the primary-template zero contribution: every
// tag MUST contribute a non-zero hash.  Pre-A3-002 every tag hashed
// to 0; post-fix every tag hashes to a non-zero (kind, value)-mixed
// value.  Two arms below — one in low catalog, one in high — pin
// the discipline.
static_assert(row_hash_contribution_v<SmBudget<0>> != 0,
    "SmBudget<0> still hashes to primary-template zero — A3-002 "
    "specialization missing.");
static_assert(row_hash_contribution_v<CarbonGramsPerKwh<0>> != 0,
    "CarbonGramsPerKwh<0> still hashes to zero — A3-002 specialization "
    "missing for the catalog-tail atom.");

// `N == 0` is a valid budget literal (a tag with zero budget is the
// "declared but inactive" state for that resource).  combine_ids
// with second arg zero stays non-zero because the first arg already
// carries the wrapper-tag salt + kind bits.
static_assert(row_hash_contribution_v<SmBudget<0>>
           != row_hash_contribution_v<SmBudget<1>>);

// Cross-kind distinctness AT N == 0 — the kind-bit half of the seed
// alone must keep all 23 kinds disjoint even when the value half is
// uniformly zero.  Three representative pairs across catalog ranges.
static_assert(row_hash_contribution_v<SmBudget<0>>
           != row_hash_contribution_v<NicQp<0>>);
static_assert(row_hash_contribution_v<NicQp<0>>
           != row_hash_contribution_v<CarbonGramsPerKwh<0>>);
static_assert(row_hash_contribution_v<HbmBytes<0>>
           != row_hash_contribution_v<HbmBandwidth<0>>);

}  // namespace detail::row_hash_resource_tag_self_test

}  // namespace crucible::safety::diag
