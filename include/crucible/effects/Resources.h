#pragma once

// The consumable-resource axes.  A kernel, compute or communication,
// declares what one call of it consumes on each axis.  A later pass
// sums the declarations across ops scheduled together and refuses to
// compile a schedule that exceeds the hardware.
//
// Units: a bandwidth axis counts bytes per second, a memory axis counts
// bytes, a power axis counts watts except the rack axis which counts
// kilowatts, the thermal axis counts degrees celsius, and every
// remaining axis is a plain count.
//
// The budget parameter is uint64_t on every axis, including the ones
// that hold small counts, because a single accelerator already carries
// tens of gigabytes of memory and trillions of bytes per second of
// bandwidth.  uint32_t would silently truncate those, and the tag is an
// empty type either way.

#include <crucible/safety/diag/RowHashFold.h>

#include <cstdint>
#include <meta>
#include <string_view>
#include <type_traits>

namespace crucible::effects {

// The underlying values are frozen.  They reach the federation cache
// keys, so renumbering one silently re-keys every cache entry already
// published by every fleet that consumed the affected rows.  A new axis
// takes the next free value and leaves the existing ones alone.
enum class ResourceKind : std::uint8_t {
    Sm = 0,
    WarpScheduler = 1,
    RegistersPerWarp = 2,
    Smem = 3,
    L2 = 4,
    HbmBytes = 5,
    HbmBw = 6,
    NvlinkBw = 7,
    PcieBw = 8,
    NicQ = 9,
    NicRing = 10,
    NicQp = 11,
    NicCq = 12,
    NicMr = 13,
    SwitchEgressBw = 14,
    SwitchBuffer = 15,
    Tcam = 16,
    CpuCore = 17,
    Llc = 18,
    PowerWatts = 19,
    ThermalCelsius = 20,
    RackPowerKw = 21,
    CarbonGramsPerKwh = 22,
};

inline constexpr std::size_t resource_kind_count = std::meta::enumerators_of(^^ResourceKind).size();

// constexpr rather than consteval so the runtime smoke test can call
// this with a non-constant argument.  Consteval contexts still fold it.
[[nodiscard]] constexpr std::string_view resource_kind_name(ResourceKind k) noexcept {
    switch (k) {
        case ResourceKind::Sm:
            return "Sm";
        case ResourceKind::WarpScheduler:
            return "WarpScheduler";
        case ResourceKind::RegistersPerWarp:
            return "RegistersPerWarp";
        case ResourceKind::Smem:
            return "Smem";
        case ResourceKind::L2:
            return "L2";
        case ResourceKind::HbmBytes:
            return "HbmBytes";
        case ResourceKind::HbmBw:
            return "HbmBw";
        case ResourceKind::NvlinkBw:
            return "NvlinkBw";
        case ResourceKind::PcieBw:
            return "PcieBw";
        case ResourceKind::NicQ:
            return "NicQ";
        case ResourceKind::NicRing:
            return "NicRing";
        case ResourceKind::NicQp:
            return "NicQp";
        case ResourceKind::NicCq:
            return "NicCq";
        case ResourceKind::NicMr:
            return "NicMr";
        case ResourceKind::SwitchEgressBw:
            return "SwitchEgressBw";
        case ResourceKind::SwitchBuffer:
            return "SwitchBuffer";
        case ResourceKind::Tcam:
            return "Tcam";
        case ResourceKind::CpuCore:
            return "CpuCore";
        case ResourceKind::Llc:
            return "Llc";
        case ResourceKind::PowerWatts:
            return "PowerWatts";
        case ResourceKind::ThermalCelsius:
            return "ThermalCelsius";
        case ResourceKind::RackPowerKw:
            return "RackPowerKw";
        case ResourceKind::CarbonGramsPerKwh:
            return "CarbonGramsPerKwh";
        default:
            return std::string_view{"<unknown ResourceKind>"};
    }
}

// The gate reads the catalog through reflection so that a new axis
// satisfies it without an edit here.  A hand-written disjunction would
// reject every future axis until someone remembered to extend it.
namespace detail {

template <ResourceKind K>
[[nodiscard]] consteval bool is_resource_kind_atom_() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^ResourceKind));
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

namespace resource {

#define CRUCIBLE_DEFINE_RESOURCE_TAG(TagName, KindEnum)                  \
    template <std::uint64_t N>                                           \
    struct TagName {                                                     \
        static constexpr ResourceKind kind = ResourceKind::KindEnum;     \
        static constexpr std::uint64_t value = N;                        \
        static constexpr std::string_view name = #TagName;               \
        constexpr TagName() noexcept = default;                          \
        constexpr TagName(const TagName&) noexcept = default;            \
        constexpr TagName(TagName&&) noexcept = default;                 \
        constexpr TagName& operator=(const TagName&) noexcept = default; \
        constexpr TagName& operator=(TagName&&) noexcept = default;      \
        ~TagName() = default;                                            \
    }

CRUCIBLE_DEFINE_RESOURCE_TAG(SmBudget, Sm);
CRUCIBLE_DEFINE_RESOURCE_TAG(WarpSchedulerSlots, WarpScheduler);
CRUCIBLE_DEFINE_RESOURCE_TAG(RegistersPerWarp, RegistersPerWarp);
CRUCIBLE_DEFINE_RESOURCE_TAG(SmemBytes, Smem);
CRUCIBLE_DEFINE_RESOURCE_TAG(L2Bytes, L2);
CRUCIBLE_DEFINE_RESOURCE_TAG(HbmBytes, HbmBytes);
CRUCIBLE_DEFINE_RESOURCE_TAG(HbmBandwidth, HbmBw);
CRUCIBLE_DEFINE_RESOURCE_TAG(NvlinkBandwidth, NvlinkBw);
CRUCIBLE_DEFINE_RESOURCE_TAG(PcieBandwidth, PcieBw);
CRUCIBLE_DEFINE_RESOURCE_TAG(NicQueueBudget, NicQ);
CRUCIBLE_DEFINE_RESOURCE_TAG(NicRingDepth, NicRing);
CRUCIBLE_DEFINE_RESOURCE_TAG(NicQp, NicQp);
CRUCIBLE_DEFINE_RESOURCE_TAG(NicCq, NicCq);
CRUCIBLE_DEFINE_RESOURCE_TAG(NicMr, NicMr);
CRUCIBLE_DEFINE_RESOURCE_TAG(SwitchEgressBw, SwitchEgressBw);
CRUCIBLE_DEFINE_RESOURCE_TAG(SwitchBufferCells, SwitchBuffer);
CRUCIBLE_DEFINE_RESOURCE_TAG(TcamEntries, Tcam);
CRUCIBLE_DEFINE_RESOURCE_TAG(CpuCoreBudget, CpuCore);
CRUCIBLE_DEFINE_RESOURCE_TAG(LlcBytes, Llc);
CRUCIBLE_DEFINE_RESOURCE_TAG(PowerWatts, PowerWatts);
CRUCIBLE_DEFINE_RESOURCE_TAG(ThermalCelsius, ThermalCelsius);
CRUCIBLE_DEFINE_RESOURCE_TAG(RackPowerKw, RackPowerKw);
CRUCIBLE_DEFINE_RESOURCE_TAG(CarbonGramsPerKwh, CarbonGramsPerKwh);

#undef CRUCIBLE_DEFINE_RESOURCE_TAG

}  // namespace resource

template <std::uint64_t N>
using SmBudget = resource::SmBudget<N>;
template <std::uint64_t N>
using WarpSchedulerSlots = resource::WarpSchedulerSlots<N>;
template <std::uint64_t N>
using RegistersPerWarp = resource::RegistersPerWarp<N>;
template <std::uint64_t N>
using SmemBytes = resource::SmemBytes<N>;
template <std::uint64_t N>
using L2Bytes = resource::L2Bytes<N>;
template <std::uint64_t N>
using HbmBytes = resource::HbmBytes<N>;
template <std::uint64_t N>
using HbmBandwidth = resource::HbmBandwidth<N>;
template <std::uint64_t N>
using NvlinkBandwidth = resource::NvlinkBandwidth<N>;
template <std::uint64_t N>
using PcieBandwidth = resource::PcieBandwidth<N>;
template <std::uint64_t N>
using NicQueueBudget = resource::NicQueueBudget<N>;
template <std::uint64_t N>
using NicRingDepth = resource::NicRingDepth<N>;
template <std::uint64_t N>
using NicQp = resource::NicQp<N>;
template <std::uint64_t N>
using NicCq = resource::NicCq<N>;
template <std::uint64_t N>
using NicMr = resource::NicMr<N>;
template <std::uint64_t N>
using SwitchEgressBw = resource::SwitchEgressBw<N>;
template <std::uint64_t N>
using SwitchBufferCells = resource::SwitchBufferCells<N>;
template <std::uint64_t N>
using TcamEntries = resource::TcamEntries<N>;
template <std::uint64_t N>
using CpuCoreBudget = resource::CpuCoreBudget<N>;
template <std::uint64_t N>
using LlcBytes = resource::LlcBytes<N>;
template <std::uint64_t N>
using PowerWatts = resource::PowerWatts<N>;
template <std::uint64_t N>
using ThermalCelsius = resource::ThermalCelsius<N>;
template <std::uint64_t N>
using RackPowerKw = resource::RackPowerKw<N>;
template <std::uint64_t N>
using CarbonGramsPerKwh = resource::CarbonGramsPerKwh<N>;

// The shape check and the catalog-membership check together, so that a
// type that merely happens to carry the three members is still
// rejected unless its kind is a real axis.
template <typename T>
concept ResourceTag = requires {
    { T::kind } -> std::convertible_to<ResourceKind>;
    { T::value } -> std::convertible_to<std::uint64_t>;
    { T::name } -> std::convertible_to<std::string_view>;
    requires IsResourceKind<T::kind>;
};

// A tag carries its three facts as static members, which reach only
// code that already knows the type.  Marshaling a row, printing a
// diagnostic, or walking a heterogeneous pack needs them as values
// instead.  This is that projection.
struct ResourceTagDescriptor {
    ResourceKind kind = ResourceKind::Sm;
    std::uint64_t value = 0;
    std::string_view name = std::string_view{"<absent>"};
};

template <ResourceTag T>
[[nodiscard]] constexpr ResourceTagDescriptor tag_descriptor() noexcept {
    return ResourceTagDescriptor{T::kind, T::value, T::name};
}

// The by-value form exists for a tag whose type is deduced from an
// argument, as when walking a tuple of tags.
template <ResourceTag T>
[[nodiscard]] constexpr ResourceTagDescriptor tag_descriptor(T /*unused*/) noexcept {
    return tag_descriptor<T>();
}

namespace detail::resources_self_test {

static_assert(resource_kind_count == 23,
              "The ResourceKind catalog has grown or shrunk.  Confirm the change is intended, give a new "
              "axis the next free underlying value rather than renumbering an existing one, and add its "
              "name arm, its tag template, and its top-level alias.");

[[nodiscard]] consteval bool every_resource_kind_has_name() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^ResourceKind));
    // -Wshadow fires spuriously on the expansion-statement induction variable.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (resource_kind_name([:en:]) == std::string_view{"<unknown ResourceKind>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_resource_kind_has_name(),
              "The resource_kind_name switch is missing an arm for at least one ResourceKind atom, so "
              "that atom reports the unknown-axis sentinel in diagnostics.");

static_assert(static_cast<std::uint8_t>(ResourceKind::Sm) == 0,
              "The value of ResourceKind::Sm changed, which invalidates every federation cache key that "
              "mentions it.");
static_assert(static_cast<std::uint8_t>(ResourceKind::WarpScheduler) == 1);
static_assert(static_cast<std::uint8_t>(ResourceKind::RegistersPerWarp) == 2);
static_assert(static_cast<std::uint8_t>(ResourceKind::Smem) == 3);
static_assert(static_cast<std::uint8_t>(ResourceKind::L2) == 4);
static_assert(static_cast<std::uint8_t>(ResourceKind::HbmBytes) == 5);
static_assert(static_cast<std::uint8_t>(ResourceKind::HbmBw) == 6);
static_assert(static_cast<std::uint8_t>(ResourceKind::NvlinkBw) == 7);
static_assert(static_cast<std::uint8_t>(ResourceKind::PcieBw) == 8);
static_assert(static_cast<std::uint8_t>(ResourceKind::NicQ) == 9);
static_assert(static_cast<std::uint8_t>(ResourceKind::NicRing) == 10);
static_assert(static_cast<std::uint8_t>(ResourceKind::NicQp) == 11);
static_assert(static_cast<std::uint8_t>(ResourceKind::NicCq) == 12);
static_assert(static_cast<std::uint8_t>(ResourceKind::NicMr) == 13);
static_assert(static_cast<std::uint8_t>(ResourceKind::SwitchEgressBw) == 14);
static_assert(static_cast<std::uint8_t>(ResourceKind::SwitchBuffer) == 15);
static_assert(static_cast<std::uint8_t>(ResourceKind::Tcam) == 16);
static_assert(static_cast<std::uint8_t>(ResourceKind::CpuCore) == 17);
static_assert(static_cast<std::uint8_t>(ResourceKind::Llc) == 18);
static_assert(static_cast<std::uint8_t>(ResourceKind::PowerWatts) == 19);
static_assert(static_cast<std::uint8_t>(ResourceKind::ThermalCelsius) == 20);
static_assert(static_cast<std::uint8_t>(ResourceKind::RackPowerKw) == 21);
static_assert(static_cast<std::uint8_t>(ResourceKind::CarbonGramsPerKwh) == 22);

// Widening the underlying type is invisible to the row hash, which
// reads only the value, but it changes the layout of every struct that
// holds a ResourceKind by value.
static_assert(std::is_same_v<std::underlying_type_t<ResourceKind>, std::uint8_t>,
              "The ResourceKind underlying type is no longer uint8_t, which changes the ABI.");

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

[[nodiscard]] consteval std::size_t count_accepted_kinds_() noexcept {
    static constexpr auto enums = std::define_static_array(std::meta::enumerators_of(^^ResourceKind));
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
              "IsResourceKind rejects an axis that is in the ResourceKind catalog.");

// A cast from an unnamed value is a well-formed ResourceKind, because
// the underlying type admits 0 through 255.  The gate must still
// reject it.
static_assert(!IsResourceKind<static_cast<ResourceKind>(99)>);
static_assert(!IsResourceKind<static_cast<ResourceKind>(255)>);
static_assert(!IsResourceKind<static_cast<ResourceKind>(23)>);

// Pairwise distinctness of the names is not asserted.  A duplicate
// would mean the same string literal typed twice in the switch, which
// review catches.
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

static_assert(std::is_default_constructible_v<resource::SmBudget<32>>);
static_assert(std::is_trivially_copyable_v<resource::SmBudget<32>>);
static_assert(std::is_trivially_destructible_v<resource::SmBudget<32>>);
static_assert(sizeof(resource::SmBudget<32>) == 1,
              "A resource tag must be 1 byte, which is the smallest an empty struct can be.");
static_assert(std::is_nothrow_default_constructible_v<resource::SmBudget<32>>);

static_assert(std::is_same_v<SmBudget<32>, resource::SmBudget<32>>);
static_assert(std::is_same_v<NicQp<4>, resource::NicQp<4>>);
static_assert(std::is_same_v<HbmBytes<80'000'000'000ULL>, resource::HbmBytes<80'000'000'000ULL>>);

static_assert(resource::SmBudget<32>::kind == ResourceKind::Sm);
static_assert(resource::SmBudget<32>::value == 32);
static_assert(resource::SmBudget<32>::name == std::string_view{"SmBudget"});

static_assert(resource::HbmBytes<80'000'000'000ULL>::kind == ResourceKind::HbmBytes);
static_assert(resource::HbmBytes<80'000'000'000ULL>::value == 80'000'000'000ULL);
static_assert(resource::HbmBytes<80'000'000'000ULL>::name == std::string_view{"HbmBytes"});

static_assert(resource::NicQp<4>::kind == ResourceKind::NicQp);
static_assert(resource::NicQp<4>::value == 4);
static_assert(resource::NicQp<4>::name == std::string_view{"NicQp"});

// One literal past the reach of uint32_t, so that a narrowing of the
// budget parameter cannot pass unnoticed.
static_assert(resource::HbmBandwidth<8'000'000'000'000ULL>::value == 8'000'000'000'000ULL,
              "A budget value was silently truncated.  The parameter must stay uint64_t.");

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

// These prove the concept rejects rather than accepting vacuously.
static_assert(!ResourceTag<int>);
static_assert(!ResourceTag<float>);
static_assert(!ResourceTag<void*>);
static_assert(!ResourceTag<ResourceKind>);

// The three tags below sit at the start of the catalog, at the far end
// of the value range, and at the end of the catalog.
static_assert(tag_descriptor<resource::SmBudget<32>>().kind == ResourceKind::Sm);
static_assert(tag_descriptor<resource::SmBudget<32>>().value == 32);
static_assert(tag_descriptor<resource::SmBudget<32>>().name == std::string_view{"SmBudget"});

static_assert(tag_descriptor<resource::HbmBytes<80'000'000'000ULL>>().kind == ResourceKind::HbmBytes);
static_assert(tag_descriptor<resource::HbmBytes<80'000'000'000ULL>>().value == 80'000'000'000ULL);

static_assert(tag_descriptor<resource::CarbonGramsPerKwh<400>>().kind == ResourceKind::CarbonGramsPerKwh);
static_assert(tag_descriptor<resource::CarbonGramsPerKwh<400>>().value == 400);

static_assert(ResourceTagDescriptor{}.kind == ResourceKind::Sm);
static_assert(ResourceTagDescriptor{}.value == 0);
static_assert(ResourceTagDescriptor{}.name == std::string_view{"<absent>"});

// Every accessor is called here with a non-constant argument.  The
// static_assert wall above only proves the constant-evaluated path, so
// a change that made one of these consteval would pass it and then
// fail at a production call site.
inline void runtime_smoke_test() {
    ResourceKind k = ResourceKind::Sm;
    [[maybe_unused]] std::string_view n1 = resource_kind_name(k);
    k = ResourceKind::WarpScheduler;
    (void)resource_kind_name(k);
    k = ResourceKind::RegistersPerWarp;
    (void)resource_kind_name(k);
    k = ResourceKind::Smem;
    (void)resource_kind_name(k);
    k = ResourceKind::L2;
    (void)resource_kind_name(k);
    k = ResourceKind::HbmBytes;
    (void)resource_kind_name(k);
    k = ResourceKind::HbmBw;
    (void)resource_kind_name(k);
    k = ResourceKind::NvlinkBw;
    (void)resource_kind_name(k);
    k = ResourceKind::PcieBw;
    (void)resource_kind_name(k);
    k = ResourceKind::NicQ;
    (void)resource_kind_name(k);
    k = ResourceKind::NicRing;
    (void)resource_kind_name(k);
    k = ResourceKind::NicQp;
    (void)resource_kind_name(k);
    k = ResourceKind::NicCq;
    (void)resource_kind_name(k);
    k = ResourceKind::NicMr;
    (void)resource_kind_name(k);
    k = ResourceKind::SwitchEgressBw;
    (void)resource_kind_name(k);
    k = ResourceKind::SwitchBuffer;
    (void)resource_kind_name(k);
    k = ResourceKind::Tcam;
    (void)resource_kind_name(k);
    k = ResourceKind::CpuCore;
    (void)resource_kind_name(k);
    k = ResourceKind::Llc;
    (void)resource_kind_name(k);
    k = ResourceKind::PowerWatts;
    (void)resource_kind_name(k);
    k = ResourceKind::ThermalCelsius;
    (void)resource_kind_name(k);
    k = ResourceKind::RackPowerKw;
    (void)resource_kind_name(k);
    k = ResourceKind::CarbonGramsPerKwh;
    (void)resource_kind_name(k);

    [[maybe_unused]] resource::SmBudget<32> sm{};
    [[maybe_unused]] resource::HbmBytes<1024> hbm{};
    [[maybe_unused]] resource::NicQp<8> qp{};
    [[maybe_unused]] resource::PowerWatts<400> pw{};

    [[maybe_unused]] auto sm_desc = tag_descriptor<resource::SmBudget<32>>();
    [[maybe_unused]] auto hbm_desc = tag_descriptor(hbm);
    [[maybe_unused]] auto qp_desc = tag_descriptor(qp);
    [[maybe_unused]] auto pw_desc = tag_descriptor(pw);
    [[maybe_unused]] auto blank = ResourceTagDescriptor{};
}

}  // namespace detail::resources_self_test

}  // namespace crucible::effects

// Without these specializations every tag falls through to the primary
// template and contributes nothing, so two wrapper stacks that differ
// only in their resource tag would share a federation cache key.
//
// The seed of each hash is the family salt byte combined with the kind
// value, which keeps the whole tag family disjoint from the other
// wrapper salts and keeps the kinds disjoint from each other.  The
// budget literal is the second argument, so two budgets of the same
// kind separate through the mixer.
//
// A new tag template needs one more line below, in catalog order.  A
// missing line shows up in the distinctness assertions at the end of
// this file, because the new tag would contribute the primary
// template's zero.
namespace crucible::safety::diag {

#define CRUCIBLE_RESOURCE_TAG_ROW_HASH(TagName, KindEnum)                                                      \
    template <std::uint64_t N>                                                                                 \
    struct row_hash_contribution<::crucible::effects::resource::TagName<N>> {                                  \
        static constexpr std::uint64_t value =                                                                 \
            detail::combine_ids(detail::WRAPPER_RESOURCE_TAG_TAG                                               \
                                    | static_cast<std::uint64_t>(::crucible::effects::ResourceKind::KindEnum), \
                                N);                                                                            \
    }

CRUCIBLE_RESOURCE_TAG_ROW_HASH(SmBudget, Sm);
CRUCIBLE_RESOURCE_TAG_ROW_HASH(WarpSchedulerSlots, WarpScheduler);
CRUCIBLE_RESOURCE_TAG_ROW_HASH(RegistersPerWarp, RegistersPerWarp);
CRUCIBLE_RESOURCE_TAG_ROW_HASH(SmemBytes, Smem);
CRUCIBLE_RESOURCE_TAG_ROW_HASH(L2Bytes, L2);
CRUCIBLE_RESOURCE_TAG_ROW_HASH(HbmBytes, HbmBytes);
CRUCIBLE_RESOURCE_TAG_ROW_HASH(HbmBandwidth, HbmBw);
CRUCIBLE_RESOURCE_TAG_ROW_HASH(NvlinkBandwidth, NvlinkBw);
CRUCIBLE_RESOURCE_TAG_ROW_HASH(PcieBandwidth, PcieBw);
CRUCIBLE_RESOURCE_TAG_ROW_HASH(NicQueueBudget, NicQ);
CRUCIBLE_RESOURCE_TAG_ROW_HASH(NicRingDepth, NicRing);
CRUCIBLE_RESOURCE_TAG_ROW_HASH(NicQp, NicQp);
CRUCIBLE_RESOURCE_TAG_ROW_HASH(NicCq, NicCq);
CRUCIBLE_RESOURCE_TAG_ROW_HASH(NicMr, NicMr);
CRUCIBLE_RESOURCE_TAG_ROW_HASH(SwitchEgressBw, SwitchEgressBw);
CRUCIBLE_RESOURCE_TAG_ROW_HASH(SwitchBufferCells, SwitchBuffer);
CRUCIBLE_RESOURCE_TAG_ROW_HASH(TcamEntries, Tcam);
CRUCIBLE_RESOURCE_TAG_ROW_HASH(CpuCoreBudget, CpuCore);
CRUCIBLE_RESOURCE_TAG_ROW_HASH(LlcBytes, Llc);
CRUCIBLE_RESOURCE_TAG_ROW_HASH(PowerWatts, PowerWatts);
CRUCIBLE_RESOURCE_TAG_ROW_HASH(ThermalCelsius, ThermalCelsius);
CRUCIBLE_RESOURCE_TAG_ROW_HASH(RackPowerKw, RackPowerKw);
CRUCIBLE_RESOURCE_TAG_ROW_HASH(CarbonGramsPerKwh, CarbonGramsPerKwh);

#undef CRUCIBLE_RESOURCE_TAG_ROW_HASH

// The pairs below are neighbours along the catalog walk rather than
// every pair, which would be 529 assertions.  A renumbering or an
// addition collides adjacent kinds first.
namespace detail::row_hash_resource_tag_self_test {

using ::crucible::effects::resource::SmBudget;
using ::crucible::effects::resource::WarpSchedulerSlots;
using ::crucible::effects::resource::NicQp;
using ::crucible::effects::resource::NicCq;
using ::crucible::effects::resource::HbmBytes;
using ::crucible::effects::resource::HbmBandwidth;
using ::crucible::effects::resource::CarbonGramsPerKwh;
using ::crucible::effects::resource::RackPowerKw;

// Two budgets of one kind must separate.
static_assert(row_hash_contribution_v<SmBudget<32>> != row_hash_contribution_v<SmBudget<64>>);
static_assert(row_hash_contribution_v<NicQp<4>> != row_hash_contribution_v<NicQp<8>>);

// Two kinds at one budget must separate.
static_assert(row_hash_contribution_v<SmBudget<32>> != row_hash_contribution_v<NicQp<32>>);
static_assert(row_hash_contribution_v<SmBudget<32>> != row_hash_contribution_v<WarpSchedulerSlots<32>>);
static_assert(row_hash_contribution_v<HbmBytes<32>> != row_hash_contribution_v<HbmBandwidth<32>>);
static_assert(row_hash_contribution_v<NicQp<32>> != row_hash_contribution_v<NicCq<32>>);
static_assert(row_hash_contribution_v<RackPowerKw<32>> != row_hash_contribution_v<CarbonGramsPerKwh<32>>);

// A zero budget is a real declaration: it says the axis is named but
// unused.  Its hash must still differ from the primary template's
// zero, which the salt in the seed provides.
static_assert(row_hash_contribution_v<SmBudget<0>> != 0,
              "The zero-budget SmBudget tag hashes like an unspecialized type.  Its row_hash "
              "specialization is missing.");
static_assert(row_hash_contribution_v<CarbonGramsPerKwh<0>> != 0,
              "The zero-budget CarbonGramsPerKwh tag hashes like an unspecialized type.  The "
              "specialization for the last axis in the catalog is missing.");

static_assert(row_hash_contribution_v<SmBudget<0>> != row_hash_contribution_v<SmBudget<1>>);

// With every budget at zero, only the kind half of the seed separates
// the axes.
static_assert(row_hash_contribution_v<SmBudget<0>> != row_hash_contribution_v<NicQp<0>>);
static_assert(row_hash_contribution_v<NicQp<0>> != row_hash_contribution_v<CarbonGramsPerKwh<0>>);
static_assert(row_hash_contribution_v<HbmBytes<0>> != row_hash_contribution_v<HbmBandwidth<0>>);

}  // namespace detail::row_hash_resource_tag_self_test

}  // namespace crucible::safety::diag
