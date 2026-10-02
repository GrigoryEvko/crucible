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
//
// Each tag names itself as its row discipline and has no payload, so the
// discipline fold in foundation/diag/RowHash.h gives every tag a slot of
// its own.  The identity is the reflected name of the tag type, which
// holds both the axis and the budget.
//
// The catalog is the enum below, and everything else is read from it by
// reflection: the name of an axis, the tag template of an axis, and the
// gate that admits an axis.  A new axis is an enumerator, a tag template
// in namespace resource and a top-level alias.  The checks at the foot of
// this header refuse an axis that lacks one of them.

#include <foundation/diag/RowHash.h>
#include <foundation/reflect/EnumName.h>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <meta>
#include <string_view>
#include <type_traits>

namespace foundation::effects {

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

inline constexpr std::size_t resource_kind_count = 23;

// True when an enumerator of the catalog holds kind.  A value cast from
// an integer that no enumerator holds is a well-formed ResourceKind, and
// the gate refuses it.  The answer is a function at namespace scope that
// is not a template, so no translation unit can specialize it.
[[nodiscard]] consteval bool is_resource_kind_atom(ResourceKind kind) {
    for (const std::meta::info enumerator : std::meta::enumerators_of(^^ResourceKind)) {
        if (std::meta::extract<ResourceKind>(std::meta::constant_of(enumerator)) == kind) return true;
    }
    return false;
}

template <ResourceKind K>
concept IsResourceKind = is_resource_kind_atom(K);

namespace resource {

#define CRUCIBLE_DEFINE_RESOURCE_TAG(TagName, KindEnum)                  \
    template <std::uint64_t N>                                           \
    struct TagName {                                                     \
        static constexpr ResourceKind kind = ResourceKind::KindEnum;     \
        static constexpr std::uint64_t value = N;                        \
        static constexpr std::string_view name = #TagName;               \
        using row_discipline = TagName;                                  \
        using row_payload = ::foundation::diag::row_payloads<>;          \
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

namespace detail {

// A budget of zero, the one argument that reads a tag template.  It is a
// function, so that only a unit that reads a tag template makes it.
[[nodiscard]] consteval std::meta::info zero_budget_() { return std::meta::reflect_constant(std::uint64_t{0}); }

// The one class template in namespace resource whose tags name axis K,
// or a reflection of void when no template or more than one template
// names K.  A member that takes no uint64_t budget, or whose tags carry
// no kind, names no axis.  Complexity: linear in the number of members
// of the namespace.
template <ResourceKind K>
[[nodiscard]] consteval std::meta::info tag_template_of_() noexcept {
    static constexpr auto members =
        std::define_static_array(std::meta::members_of(^^resource, std::meta::access_context::current()));
    std::meta::info found = ^^void;
    std::size_t matches = 0;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto member : members) {
        if constexpr (std::meta::is_class_template(member) && std::meta::can_substitute(member, {zero_budget_()})) {
            using probe = typename[:std::meta::substitute(member, {zero_budget_()}):];
            if constexpr (requires {
                              { probe::kind } -> std::convertible_to<ResourceKind>;
                          }) {
                if constexpr (probe::kind == K) {
                    found = member;
                    ++matches;
                }
            }
        }
    }
#pragma GCC diagnostic pop
    return matches == 1 ? found : ^^void;
}

// True when T is a specialization of the tag template of its own axis,
// and its value and name are the budget and the template it spells.  A
// type that copies the three members of a tag is not one, and neither is
// an explicit specialization that states another budget.
template <typename T>
[[nodiscard]] consteval bool is_axis_tag_() noexcept {
    return std::meta::has_template_arguments(^^T) && std::meta::template_of(^^T) == tag_template_of_<T::kind>()
        && std::meta::extract<std::uint64_t>(std::meta::template_arguments_of(^^T)[0]) == T::value
        && std::meta::identifier_of(std::meta::template_of(^^T)) == T::name;
}

}  // namespace detail

// A resource tag is an unqualified specialization of the tag template of
// a catalog axis, so one budget has one spelling.  The kind is read
// first, so a kind that names no axis is refused before the template
// check reads it.
template <typename T>
concept ResourceTag = std::same_as<T, std::remove_cv_t<T>> && requires {
    { T::kind } -> std::convertible_to<ResourceKind>;
    { T::value } -> std::convertible_to<std::uint64_t>;
    { T::name } -> std::convertible_to<std::string_view>;
    requires IsResourceKind<T::kind>;
    requires detail::is_axis_tag_<T>();
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

}  // namespace foundation::effects
