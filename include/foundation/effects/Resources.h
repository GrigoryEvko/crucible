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

inline constexpr std::size_t resource_kind_count = std::meta::enumerators_of(^^ResourceKind).size();

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

// A budget of zero, the one argument that reads a tag template.
inline constexpr std::meta::info zero_budget_ = std::meta::reflect_constant(std::uint64_t{0});

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
        if constexpr (std::meta::is_class_template(member) && std::meta::can_substitute(member, {zero_budget_})) {
            using probe = typename[:std::meta::substitute(member, {zero_budget_}):];
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

namespace detail::resources_self_test {

static_assert(resource_kind_count == 23,
              "The ResourceKind catalog has grown or shrunk.  Confirm the change is intended.  Give a new "
              "axis the next free underlying value, a tag template in namespace resource and a top-level "
              "alias.  Do not renumber an existing axis.");

// Each axis has exactly one tag template, and its tags are resource
// tags.  Each tag template has a top-level alias of its own name.
[[nodiscard]] consteval bool every_axis_has_one_tag_template() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^ResourceKind));
    bool all_found = true;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        constexpr ResourceKind kind = [:en:];
        constexpr std::meta::info tag = tag_template_of_<kind>();
        if constexpr (tag == ^^void) {
            all_found = false;
        } else {
            all_found = all_found && ResourceTag<typename[:std::meta::substitute(tag, {zero_budget_}):]>;
        }
    }
#pragma GCC diagnostic pop
    return all_found;
}
static_assert(every_axis_has_one_tag_template(),
              "An axis of ResourceKind has no tag template in namespace resource, it has two, or its tags are "
              "no resource tags.  Define exactly one with CRUCIBLE_DEFINE_RESOURCE_TAG.");

// The alias must name the tag it is spelled after, at a budget of zero.
[[nodiscard]] consteval bool every_tag_template_has_an_alias() noexcept {
    const auto context = std::meta::access_context::current();
    for (const auto tag : std::meta::members_of(^^resource, context)) {
        if (!std::meta::is_class_template(tag)) continue;
        const auto zero_tag = std::meta::substitute(tag, {zero_budget_});
        bool aliased = false;
        for (const auto alias : std::meta::members_of(^^::foundation::effects, context)) {
            if (!std::meta::is_alias_template(alias)
                || std::meta::identifier_of(alias) != std::meta::identifier_of(tag)) {
                continue;
            }
            aliased = aliased || std::meta::dealias(std::meta::substitute(alias, {zero_budget_})) == zero_tag;
        }
        if (!aliased) return false;
    }
    return true;
}
static_assert(every_tag_template_has_an_alias(),
              "A tag template in namespace resource has no top-level alias of its name in "
              "foundation::effects.  Add the alias beside the others.");

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

// The name of an axis is the identifier of its enumerator.  A value
// that no enumerator holds reads as the sentinel.
static_assert(::foundation::reflect::enum_name(ResourceKind::Sm) == std::string_view{"Sm"});
static_assert(::foundation::reflect::enum_name(ResourceKind::CarbonGramsPerKwh)
              == std::string_view{"CarbonGramsPerKwh"});
static_assert(::foundation::reflect::enum_name(static_cast<ResourceKind>(99))
              == std::string_view{"<unknown ResourceKind>"});

static_assert(std::is_default_constructible_v<resource::SmBudget<32>>);
static_assert(std::is_trivially_copyable_v<resource::SmBudget<32>>);
static_assert(std::is_trivially_destructible_v<resource::SmBudget<32>>);
static_assert(sizeof(resource::SmBudget<32>) == 1,
              "A resource tag must be 1 byte, which is the smallest an empty struct can be.");
static_assert(std::is_nothrow_default_constructible_v<resource::SmBudget<32>>);

static_assert(std::is_same_v<SmBudget<32>, resource::SmBudget<32>>);
static_assert(std::is_same_v<NicQp<4>, resource::NicQp<4>>);
static_assert(std::is_same_v<HbmBytes<80000000000ULL>, resource::HbmBytes<80000000000ULL>>);

static_assert(resource::SmBudget<32>::kind == ResourceKind::Sm);
static_assert(resource::SmBudget<32>::value == 32);
static_assert(resource::SmBudget<32>::name == std::string_view{"SmBudget"});

static_assert(resource::HbmBytes<80000000000ULL>::kind == ResourceKind::HbmBytes);
static_assert(resource::HbmBytes<80000000000ULL>::value == 80000000000ULL);
static_assert(resource::HbmBytes<80000000000ULL>::name == std::string_view{"HbmBytes"});

// One literal past the reach of uint32_t, so that a narrowing of the
// budget parameter cannot pass unnoticed.
static_assert(resource::HbmBandwidth<8000000000000ULL>::value == 8000000000000ULL,
              "A budget value was silently truncated.  The parameter must stay uint64_t.");

// The first and the last axis of the catalog, and one past the reach of
// uint32_t.  The walk above admits a tag of every axis.
static_assert(ResourceTag<resource::SmBudget<32>>);
static_assert(ResourceTag<resource::CarbonGramsPerKwh<400>>);
static_assert(ResourceTag<HbmBytes<80000000000ULL>>);

// These prove the concept rejects rather than accepting vacuously.
static_assert(!ResourceTag<int>);
static_assert(!ResourceTag<float>);
static_assert(!ResourceTag<void*>);
static_assert(!ResourceTag<ResourceKind>);

// A type with the three members of a tag and a real axis is still no
// tag, because it is no specialization of the tag template of its axis.
struct lookalike_tag {
    static constexpr ResourceKind kind = ResourceKind::Sm;
    static constexpr std::uint64_t value = 32;
    static constexpr std::string_view name = "SmBudget";
};
static_assert(!ResourceTag<lookalike_tag>);

// A qualified tag would be a second spelling of one budget.
static_assert(!ResourceTag<const resource::SmBudget<32>>);
static_assert(!ResourceTag<volatile resource::SmBudget<32>>);

// The three tags below sit at the start of the catalog, at the far end
// of the value range, and at the end of the catalog.
static_assert(tag_descriptor<resource::SmBudget<32>>().kind == ResourceKind::Sm);
static_assert(tag_descriptor<resource::SmBudget<32>>().value == 32);
static_assert(tag_descriptor<resource::SmBudget<32>>().name == std::string_view{"SmBudget"});

static_assert(tag_descriptor<resource::HbmBytes<80000000000ULL>>().kind == ResourceKind::HbmBytes);
static_assert(tag_descriptor<resource::HbmBytes<80000000000ULL>>().value == 80000000000ULL);

static_assert(tag_descriptor<resource::CarbonGramsPerKwh<400>>().kind == ResourceKind::CarbonGramsPerKwh);
static_assert(tag_descriptor<resource::CarbonGramsPerKwh<400>>().value == 400);

static_assert(ResourceTagDescriptor{}.kind == ResourceKind::Sm);
static_assert(ResourceTagDescriptor{}.value == 0);
static_assert(ResourceTagDescriptor{}.name == std::string_view{"<absent>"});

// The run-time half of these checks lives in
// test/foundation/test_effects_resources.cpp, which calls every
// accessor with values the optimizer cannot fold.

}  // namespace detail::resources_self_test

// Without a row hash every tag takes the zero slot that a bare payload
// has, so two wrapper stacks that differ only in their resource tag
// would share a federation cache key.  The discipline fold reaches each
// tag through the two member aliases the tag macro writes.
//
// The pairs below are neighbours along the catalog walk rather than
// every pair, which would be 529 assertions.  A renumbering or an
// addition collides adjacent kinds first.
namespace detail::row_hash_resource_tag_self_test {

using ::foundation::diag::row_hash_contribution_v;
using resource::CarbonGramsPerKwh;
using resource::HbmBandwidth;
using resource::HbmBytes;
using resource::NicCq;
using resource::NicQp;
using resource::RackPowerKw;
using resource::SmBudget;
using resource::WarpSchedulerSlots;

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
// unused.  Its hash must still differ from the zero that a bare payload
// has, which the discipline salt provides.
static_assert(row_hash_contribution_v<SmBudget<0>> != 0,
              "The zero-budget SmBudget tag hashes like a bare payload.  Its discipline aliases are missing.");
static_assert(row_hash_contribution_v<CarbonGramsPerKwh<0>> != 0,
              "The zero-budget CarbonGramsPerKwh tag hashes like a bare payload.  The discipline aliases of "
              "the last axis in the catalog are missing.");

static_assert(row_hash_contribution_v<SmBudget<0>> != row_hash_contribution_v<SmBudget<1>>);

// With every budget at zero, only the kind in the tag name separates
// the axes.
static_assert(row_hash_contribution_v<SmBudget<0>> != row_hash_contribution_v<NicQp<0>>);
static_assert(row_hash_contribution_v<NicQp<0>> != row_hash_contribution_v<CarbonGramsPerKwh<0>>);
static_assert(row_hash_contribution_v<HbmBytes<0>> != row_hash_contribution_v<HbmBandwidth<0>>);

}  // namespace detail::row_hash_resource_tag_self_test

}  // namespace foundation::effects
