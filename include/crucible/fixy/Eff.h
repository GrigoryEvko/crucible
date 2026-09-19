#pragma once

#include <crucible/effects/Effects.h>
#include <crucible/effects/_Capability.h>
#include <crucible/effects/_ExecCtx.h>
#include <crucible/effects/Resources.h>
#include <crucible/effects/Concurrent.h>
#include <crucible/effects/CtxWrapperLift.h>
#include <crucible/effects/FxAliases.h>

#include <type_traits>

namespace crucible::fixy {
// Do not add a using-decl for an individual symbol on top of this alias. It
// would redeclare the same name in the same namespace. Every substrate name
// is already reachable as fixy::eff::X.
namespace eff = ::crucible::effects;
}  // namespace crucible::fixy

namespace crucible::fixy::eff_self_test {

static_assert(::crucible::fixy::eff::Effect::Alloc == ::crucible::effects::Effect::Alloc);
static_assert(::crucible::fixy::eff::Effect::Bg == ::crucible::effects::Effect::Bg);
static_assert(::crucible::fixy::eff::effect_count == ::crucible::effects::effect_count);

static_assert(std::is_same_v<::crucible::fixy::eff::Row<::crucible::effects::Effect::Alloc>,
                             ::crucible::effects::Row<::crucible::effects::Effect::Alloc>>,
              "fixy::eff::Row must BE effects::Row under the namespace alias.");

static_assert(::crucible::fixy::eff::Subrow<::crucible::fixy::eff::Row<>,
                                            ::crucible::fixy::eff::Row<::crucible::effects::Effect::Alloc>>);

static_assert(std::is_same_v<::crucible::fixy::eff::Computation<::crucible::fixy::eff::Row<>, int>,
                             ::crucible::effects::Computation<::crucible::effects::Row<>, int>>,
              "fixy::eff::Computation must BE effects::Computation.");

static_assert(
    std::is_same_v<::crucible::fixy::eff::Capability<::crucible::effects::Effect::Alloc, ::crucible::fixy::eff::Bg>,
                   ::crucible::effects::Capability<::crucible::effects::Effect::Alloc, ::crucible::effects::Bg>>,
    "fixy::eff::Capability must BE effects::Capability.");

static_assert(::crucible::fixy::eff::IsPure<::crucible::fixy::eff::PureRow>);
static_assert(::crucible::fixy::eff::IsTot<::crucible::fixy::eff::TotRow>);
static_assert(::crucible::fixy::eff::IsST<::crucible::fixy::eff::STRow>);
static_assert(::crucible::fixy::eff::IsAll<::crucible::fixy::eff::AllRow>);

static_assert(::crucible::fixy::eff::IsExecCtx<::crucible::fixy::eff::HotFgCtx>);
static_assert(::crucible::fixy::eff::IsExecCtx<::crucible::fixy::eff::BgDrainCtx>);
static_assert(::crucible::fixy::eff::IsBgCtx<::crucible::fixy::eff::BgDrainCtx>);
static_assert(::crucible::fixy::eff::IsFgCtx<::crucible::fixy::eff::HotFgCtx>);

static_assert(sizeof(::crucible::fixy::eff::Bg) == 1);
static_assert(sizeof(::crucible::fixy::eff::Init) == 1);
static_assert(sizeof(::crucible::fixy::eff::Test) == 1);

static_assert(::crucible::fixy::eff::resource_kind_count >= 23,
              "fixy::eff::resource_kind_count regressed below its floor of 23. "
              "A ResourceKind enumerator was removed without updating both the "
              "exact pin beside the constant and this floor witness.");

static_assert(std::is_same_v<::crucible::fixy::eff::resource::SmBudget<1>, ::crucible::effects::resource::SmBudget<1>>);
static_assert(std::is_same_v<::crucible::fixy::eff::resource::WarpSchedulerSlots<1>,
                             ::crucible::effects::resource::WarpSchedulerSlots<1>>);
static_assert(std::is_same_v<::crucible::fixy::eff::resource::RegistersPerWarp<1>,
                             ::crucible::effects::resource::RegistersPerWarp<1>>);
static_assert(
    std::is_same_v<::crucible::fixy::eff::resource::SmemBytes<1>, ::crucible::effects::resource::SmemBytes<1>>);
static_assert(std::is_same_v<::crucible::fixy::eff::resource::L2Bytes<1>, ::crucible::effects::resource::L2Bytes<1>>);
static_assert(std::is_same_v<::crucible::fixy::eff::resource::HbmBytes<1>, ::crucible::effects::resource::HbmBytes<1>>);
static_assert(
    std::is_same_v<::crucible::fixy::eff::resource::HbmBandwidth<1>, ::crucible::effects::resource::HbmBandwidth<1>>);
static_assert(std::is_same_v<::crucible::fixy::eff::resource::NvlinkBandwidth<1>,
                             ::crucible::effects::resource::NvlinkBandwidth<1>>);
static_assert(
    std::is_same_v<::crucible::fixy::eff::resource::PcieBandwidth<1>, ::crucible::effects::resource::PcieBandwidth<1>>);
static_assert(std::is_same_v<::crucible::fixy::eff::resource::NicQueueBudget<1>,
                             ::crucible::effects::resource::NicQueueBudget<1>>);
static_assert(
    std::is_same_v<::crucible::fixy::eff::resource::NicRingDepth<1>, ::crucible::effects::resource::NicRingDepth<1>>);
static_assert(std::is_same_v<::crucible::fixy::eff::resource::NicQp<1>, ::crucible::effects::resource::NicQp<1>>);
static_assert(std::is_same_v<::crucible::fixy::eff::resource::NicCq<1>, ::crucible::effects::resource::NicCq<1>>);
static_assert(std::is_same_v<::crucible::fixy::eff::resource::NicMr<1>, ::crucible::effects::resource::NicMr<1>>);
static_assert(std::is_same_v<::crucible::fixy::eff::resource::SwitchEgressBw<1>,
                             ::crucible::effects::resource::SwitchEgressBw<1>>);
static_assert(std::is_same_v<::crucible::fixy::eff::resource::SwitchBufferCells<1>,
                             ::crucible::effects::resource::SwitchBufferCells<1>>);
static_assert(
    std::is_same_v<::crucible::fixy::eff::resource::TcamEntries<1>, ::crucible::effects::resource::TcamEntries<1>>);
static_assert(
    std::is_same_v<::crucible::fixy::eff::resource::CpuCoreBudget<1>, ::crucible::effects::resource::CpuCoreBudget<1>>);
static_assert(std::is_same_v<::crucible::fixy::eff::resource::LlcBytes<1>, ::crucible::effects::resource::LlcBytes<1>>);
static_assert(
    std::is_same_v<::crucible::fixy::eff::resource::PowerWatts<1>, ::crucible::effects::resource::PowerWatts<1>>);
static_assert(std::is_same_v<::crucible::fixy::eff::resource::ThermalCelsius<1>,
                             ::crucible::effects::resource::ThermalCelsius<1>>);
static_assert(
    std::is_same_v<::crucible::fixy::eff::resource::RackPowerKw<1>, ::crucible::effects::resource::RackPowerKw<1>>);
static_assert(std::is_same_v<::crucible::fixy::eff::resource::CarbonGramsPerKwh<1>,
                             ::crucible::effects::resource::CarbonGramsPerKwh<1>>);

}  // namespace crucible::fixy::eff_self_test
