// The compile-time checks of foundation/effects/Resources.h.

#include <foundation/effects/Resources.h>

namespace foundation::effects {

namespace detail::resources_self_test {

static_assert(resource_kind_count == std::meta::enumerators_of(^^ResourceKind).size(),
              "The ResourceKind catalog has grown or shrunk.  Confirm the change is intended.  Give a new "
              "axis the next free underlying value, a tag template in namespace resource and a top-level "
              "alias, and write the new count in the initializer of resource_kind_count.  Do not renumber "
              "an existing axis.");

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
            all_found = all_found && ResourceTag<typename[:std::meta::substitute(tag, {zero_budget_()}):]>;
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
        const auto zero_tag = std::meta::substitute(tag, {zero_budget_()});
        bool aliased = false;
        for (const auto alias : std::meta::members_of(^^::foundation::effects, context)) {
            if (!std::meta::is_alias_template(alias)
                || std::meta::identifier_of(alias) != std::meta::identifier_of(tag)) {
                continue;
            }
            aliased = aliased || std::meta::dealias(std::meta::substitute(alias, {zero_budget_()})) == zero_tag;
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
