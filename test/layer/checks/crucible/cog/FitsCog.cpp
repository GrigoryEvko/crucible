// The compile-time checks of crucible/cog/FitsCog.h.

#include <crucible/cog/FitsCog.h>

namespace crucible::cog {

namespace detail::fits_cog_self_test {

static_assert(HasCogCapacity<CogKind::Gpu>);
static_assert(HasCogCapacity<CogKind::CpuCore>);
static_assert(HasCogCapacity<CogKind::CpuSocket>);
static_assert(HasCogCapacity<CogKind::NicPort>);
static_assert(HasCogCapacity<CogKind::NvSwitch>);
static_assert(HasCogCapacity<CogKind::DramChannel>);

static_assert(!HasCogCapacity<CogKind::PsuRail>);
static_assert(!HasCogCapacity<CogKind::BmcSensor>);
static_assert(!HasCogCapacity<CogKind::OpticalTransceiver>);
static_assert(!HasCogCapacity<CogKind::NvmeNamespace>);
static_assert(!HasCogCapacity<CogKind::PcieLaneGroup>);
static_assert(!HasCogCapacity<CogKind::Datacenter>);
static_assert(!HasCogCapacity<CogKind::Rack>);
static_assert(!HasCogCapacity<CogKind::Server>);
static_assert(!HasCogCapacity<CogKind::GpuPackage>);
static_assert(!HasCogCapacity<CogKind::NicCard>);

// The ceiling table and the schema table stay in step. The runtime
// helper needs both for the same kind, so a kind that has one but not
// the other cannot be checked at all.
static_assert(HasCaps<CogKind::Gpu> == HasCogCapacity<CogKind::Gpu>);
static_assert(HasCaps<CogKind::CpuCore> == HasCogCapacity<CogKind::CpuCore>);
static_assert(HasCaps<CogKind::CpuSocket> == HasCogCapacity<CogKind::CpuSocket>);
static_assert(HasCaps<CogKind::NicPort> == HasCogCapacity<CogKind::NicPort>);
static_assert(HasCaps<CogKind::NvSwitch> == HasCogCapacity<CogKind::NvSwitch>);
static_assert(HasCaps<CogKind::DramChannel> == HasCogCapacity<CogKind::DramChannel>);
static_assert(HasCaps<CogKind::PsuRail> == HasCogCapacity<CogKind::PsuRail>);

// A kind with a ceiling table but no runtime mapping only fails when
// somebody first calls the runtime helper on it, far from the
// omission. These assertions move that failure back to the table.
template <CogKind K>
inline constexpr bool has_caps_runtime_capacity_v =
    requires(caps_for_t<K> const& caps, ::foundation::effects::ResourceKind axis) {
        { detail::caps_runtime_capacity<K>::for_kind(caps, axis) } -> std::same_as<std::uint64_t>;
    };
static_assert(has_caps_runtime_capacity_v<CogKind::Gpu>);
static_assert(has_caps_runtime_capacity_v<CogKind::NicPort>);
static_assert(has_caps_runtime_capacity_v<CogKind::NvSwitch>);
static_assert(has_caps_runtime_capacity_v<CogKind::CpuCore>);
static_assert(has_caps_runtime_capacity_v<CogKind::CpuSocket>);
static_assert(has_caps_runtime_capacity_v<CogKind::DramChannel>);
static_assert(!has_caps_runtime_capacity_v<CogKind::PsuRail>);
static_assert(!has_caps_runtime_capacity_v<CogKind::BmcSensor>);
static_assert(!has_caps_runtime_capacity_v<CogKind::Datacenter>);

// A specialisation that returns zero on every axis rejects every row
// forever, which is almost always a mistake. DRAM channels are the one
// deliberate case, so they are absent from this list.
static_assert(cog_max_capacity<CogKind::Gpu>::for_kind(::foundation::effects::ResourceKind::Sm) > 0);
static_assert(cog_max_capacity<CogKind::Gpu>::for_kind(::foundation::effects::ResourceKind::HbmBytes) > 0);
static_assert(cog_max_capacity<CogKind::CpuCore>::for_kind(::foundation::effects::ResourceKind::CpuCore) > 0);
static_assert(cog_max_capacity<CogKind::CpuSocket>::for_kind(::foundation::effects::ResourceKind::CpuCore) > 0);
static_assert(cog_max_capacity<CogKind::NicPort>::for_kind(::foundation::effects::ResourceKind::NicQp) > 0);
static_assert(cog_max_capacity<CogKind::NvSwitch>::for_kind(::foundation::effects::ResourceKind::SwitchEgressBw) > 0);

static_assert(cog_max_capacity<CogKind::Gpu>::for_kind(::foundation::effects::ResourceKind::NicQp) == 0);
static_assert(cog_max_capacity<CogKind::Gpu>::for_kind(::foundation::effects::ResourceKind::SwitchEgressBw) == 0);
static_assert(cog_max_capacity<CogKind::Gpu>::for_kind(::foundation::effects::ResourceKind::CpuCore) == 0);
static_assert(cog_max_capacity<CogKind::NicPort>::for_kind(::foundation::effects::ResourceKind::Sm) == 0);
static_assert(cog_max_capacity<CogKind::NicPort>::for_kind(::foundation::effects::ResourceKind::HbmBytes) == 0);
static_assert(cog_max_capacity<CogKind::NvSwitch>::for_kind(::foundation::effects::ResourceKind::Sm) == 0);
static_assert(cog_max_capacity<CogKind::NvSwitch>::for_kind(::foundation::effects::ResourceKind::NicQp) == 0);
static_assert(cog_max_capacity<CogKind::CpuCore>::for_kind(::foundation::effects::ResourceKind::Sm) == 0);
static_assert(cog_max_capacity<CogKind::CpuCore>::for_kind(::foundation::effects::ResourceKind::NicQp) == 0);

using H100ComputeRow = ::foundation::effects::ConcurrentRow<::foundation::effects::SmBudget<132>,
                                                            ::foundation::effects::HbmBytes<80000000000ULL>>;
static_assert(FitsCog<H100ComputeRow, CogKind::Gpu>);

using NicAllReduceRow =
    ::foundation::effects::ConcurrentRow<::foundation::effects::NicQp<4>, ::foundation::effects::NicCq<4>,
                                         ::foundation::effects::NicMr<8>>;
static_assert(FitsCog<NicAllReduceRow, CogKind::NicPort>);

using SwitchRow = ::foundation::effects::ConcurrentRow<::foundation::effects::SwitchEgressBw<400000000000ULL>,
                                                       ::foundation::effects::SwitchBufferCells<32 * 1024>>;
static_assert(FitsCog<SwitchRow, CogKind::NvSwitch>);

using SocketRow = ::foundation::effects::ConcurrentRow<::foundation::effects::CpuCoreBudget<64>,
                                                       ::foundation::effects::LlcBytes<128 * 1024 * 1024>>;
static_assert(FitsCog<SocketRow, CogKind::CpuSocket>);

// A row that asks for nothing satisfies every axis vacuously.
static_assert(FitsCog<::foundation::effects::ConcurrentRow<>, CogKind::Gpu>);
static_assert(FitsCog<::foundation::effects::ConcurrentRow<>, CogKind::NicPort>);
static_assert(FitsCog<::foundation::effects::ConcurrentRow<>, CogKind::NvSwitch>);

using OversubscribedSmRow = ::foundation::effects::ConcurrentRow<::foundation::effects::SmBudget<999>>;
static_assert(!FitsCog<OversubscribedSmRow, CogKind::Gpu>);

using OversubscribedHbmRow =
    ::foundation::effects::ConcurrentRow<::foundation::effects::HbmBytes<512ULL * 1024 * 1024 * 1024>>;
static_assert(!FitsCog<OversubscribedHbmRow, CogKind::Gpu>);

using ConcurrentOverSubRow = ::foundation::effects::concurrent_row_sum_t<
    ::foundation::effects::ConcurrentRow<::foundation::effects::SmBudget<200>>,
    ::foundation::effects::ConcurrentRow<::foundation::effects::SmBudget<200>>>;
static_assert(!FitsCog<ConcurrentOverSubRow, CogKind::Gpu>);
static_assert(
    ::foundation::effects::concurrent_row_value_v<::foundation::effects::ResourceKind::Sm, ConcurrentOverSubRow>
    == 400);

// A demand on an axis the kind does not expose fails, because that
// axis has a ceiling of zero.
using NicDemandOnGpu = ::foundation::effects::ConcurrentRow<::foundation::effects::NicQp<4>>;
static_assert(!FitsCog<NicDemandOnGpu, CogKind::Gpu>);

using GpuDemandOnNic = ::foundation::effects::ConcurrentRow<::foundation::effects::SmBudget<4>>;
static_assert(!FitsCog<GpuDemandOnNic, CogKind::NicPort>);

using SwitchDemandOnCpu = ::foundation::effects::ConcurrentRow<::foundation::effects::SwitchEgressBw<100000000000ULL>>;
static_assert(!FitsCog<SwitchDemandOnCpu, CogKind::CpuSocket>);

static_assert(!FitsCog<H100ComputeRow, CogKind::PsuRail>);
static_assert(!FitsCog<H100ComputeRow, CogKind::BmcSensor>);
static_assert(!FitsCog<H100ComputeRow, CogKind::OpticalTransceiver>);
static_assert(!FitsCog<::foundation::effects::ConcurrentRow<>, CogKind::PsuRail>);

// A single budget tag is not a row, and neither is a plain type.
static_assert(!FitsCog<int, CogKind::Gpu>);
static_assert(!FitsCog<::foundation::effects::resource::SmBudget<32>, CogKind::Gpu>);

// A row that exactly saturates a ceiling still admits, because some
// Cog of that kind has exactly that capacity.
using SaturateGpuSm = ::foundation::effects::ConcurrentRow<::foundation::effects::SmBudget<320>>;
static_assert(FitsCog<SaturateGpuSm, CogKind::Gpu>);

using SaturateGpuHbm =
    ::foundation::effects::ConcurrentRow<::foundation::effects::HbmBytes<384ULL * 1024 * 1024 * 1024>>;
static_assert(FitsCog<SaturateGpuHbm, CogKind::Gpu>);

using OneOverGpuSm = ::foundation::effects::ConcurrentRow<::foundation::effects::SmBudget<321>>;
static_assert(!FitsCog<OneOverGpuSm, CogKind::Gpu>);

}  // namespace detail::fits_cog_self_test

}  // namespace crucible::cog
