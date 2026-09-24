#pragma once

#include <crucible/cog/CogIdentity.h>
#include <crucible/cog/TargetCaps.h>
#include <crucible/effects/Concurrent.h>
#include <crucible/effects/Resources.h>

#include <concepts>
#include <cstdint>
#include <meta>
#include <type_traits>

namespace crucible::cog {

// A specialisation publishes, for every resource axis, the largest
// capacity any shipped Cog of that kind has. An axis the kind does not
// expose reads 0, which rejects any non-zero demand on it.
//
// A ceiling is the maximum across the whole family plus headroom, not
// a nominal part. Raising one when newer hardware ships admits more
// and breaks nothing. Lowering one rejects rows that used to fit.
//
// The primary template stays undefined, so an unrecognised kind fails
// at name lookup rather than reading a default of zero.
//
// The axis fold below is reflection-driven and picks up a new resource
// axis on its own. A specialisation that does not add a case for the
// new axis gives it a ceiling of 0 and rejects every demand on it,
// which is wrong-but-safe rather than silently permissive.

template <CogKind K>
struct cog_max_capacity;

template <>
struct cog_max_capacity<CogKind::Gpu> {
    // constexpr rather than consteval so a runtime caller can walk
    // every axis through this accessor with non-constant arguments.
    // The compile-time gate forces constant evaluation on its own.
    static constexpr std::uint64_t for_kind(effects::ResourceKind k) noexcept {
        using effects::ResourceKind;
        switch (k) {
            case ResourceKind::Sm:
                return 320ULL;  // largest shipped die is 256, plus headroom
            case ResourceKind::WarpScheduler:
                return 320ULL * 4;  // four schedulers per SM
            case ResourceKind::RegistersPerWarp:
                return 65536ULL;  // 256 KB of registers at 4 bytes each
            case ResourceKind::Smem:
                return 320ULL * 256ULL * 1024;  // 256 KB per SM
            case ResourceKind::L2:
                return 100ULL * 1024 * 1024;
            case ResourceKind::HbmBytes:
                return 384ULL * 1024 * 1024 * 1024;  // largest stack is 288 GB
            case ResourceKind::HbmBw:
                return 9ULL * 1024 * 1024 * 1024 * 1024;
            case ResourceKind::NvlinkBw:
                return 1800ULL * 1024 * 1024 * 1024;
            // PCIe bandwidth follows from the generation and lane count,
            // and no schema field carries the product yet. A ceiling of
            // 0 rejects every demand on the axis, which matches what the
            // runtime side reports for it. Both layers stay consistent
            // until a measured field exists to read.
            case ResourceKind::PcieBw:
                return 0ULL;
            case ResourceKind::PowerWatts:
                return 1500ULL;
            case ResourceKind::ThermalCelsius:
                return 95ULL;
            default:
                return 0ULL;
        }
    }
};

template <>
struct cog_max_capacity<CogKind::NicPort> {
    static constexpr std::uint64_t for_kind(effects::ResourceKind k) noexcept {
        using effects::ResourceKind;
        switch (k) {
            case ResourceKind::PcieBw:
                return 0ULL;
            case ResourceKind::NicQ:
                return 256ULL;  // transmit and receive queues together
            case ResourceKind::NicRing:
                return 64ULL * 1024;  // ring slots
            case ResourceKind::NicQp:
                return 16ULL * 1024 * 1024;
            case ResourceKind::NicCq:
                return 16ULL * 1024 * 1024;  // one completion queue per pair
            case ResourceKind::NicMr:
                return 64ULL * 1024;
            case ResourceKind::PowerWatts:
                return 50ULL;
            case ResourceKind::ThermalCelsius:
                return 85ULL;
            default:
                return 0ULL;
        }
    }
};

template <>
struct cog_max_capacity<CogKind::NvSwitch> {
    static constexpr std::uint64_t for_kind(effects::ResourceKind k) noexcept {
        using effects::ResourceKind;
        switch (k) {
            case ResourceKind::SwitchEgressBw:
                return 32ULL * 1024 * 1024 * 1024 * 1024;
            case ResourceKind::SwitchBuffer:
                return 128ULL * 1024;  // buffer cells
            case ResourceKind::Tcam:
                return 64ULL * 1024;  // entries
            case ResourceKind::PowerWatts:
                return 1500ULL;
            case ResourceKind::ThermalCelsius:
                return 90ULL;
            default:
                return 0ULL;
        }
    }
};

// A core exposes no last-level cache. That capacity belongs to the
// socket.
template <>
struct cog_max_capacity<CogKind::CpuCore> {
    static constexpr std::uint64_t for_kind(effects::ResourceKind k) noexcept {
        using effects::ResourceKind;
        switch (k) {
            case ResourceKind::CpuCore:
                return 1ULL;  // the core is the atom
            case ResourceKind::L2:
                return 4ULL * 1024 * 1024;  // private to the core
            case ResourceKind::PowerWatts:
                return 50ULL;
            case ResourceKind::ThermalCelsius:
                return 100ULL;
            default:
                return 0ULL;
        }
    }
};

template <>
struct cog_max_capacity<CogKind::CpuSocket> {
    static constexpr std::uint64_t for_kind(effects::ResourceKind k) noexcept {
        using effects::ResourceKind;
        switch (k) {
            case ResourceKind::CpuCore:
                return 256ULL;
            case ResourceKind::Llc:
                return 1024ULL * 1024 * 1024;
            case ResourceKind::L2:
                return 256ULL * 4ULL * 1024 * 1024;  // per-core L2 across the socket
            case ResourceKind::PowerWatts:
                return 800ULL;
            case ResourceKind::ThermalCelsius:
                return 100ULL;
            default:
                return 0ULL;
        }
    }
};

// Every axis is zero on purpose. A row accounts its memory against the
// GPU memory axes, and no resource axis names host DRAM, so a DRAM
// channel has nothing a row can ask it for.
template <>
struct cog_max_capacity<CogKind::DramChannel> {
    static constexpr std::uint64_t for_kind(effects::ResourceKind k) noexcept {
        using effects::ResourceKind;
        switch (k) {
            default:
                return 0ULL;
        }
    }
};

template <CogKind K>
concept HasCogCapacity = requires(effects::ResourceKind axis) {
    { cog_max_capacity<K>::for_kind(axis) } -> std::same_as<std::uint64_t>;
};

namespace detail {

template <typename Row, CogKind K>
[[nodiscard]] consteval bool evaluate_row_fits_cog() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^effects::ResourceKind));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        constexpr effects::ResourceKind axis = [:en:];
        constexpr std::uint64_t demand = effects::concurrent_row_value_v<axis, Row>;
        constexpr std::uint64_t ceiling = cog_max_capacity<K>::for_kind(axis);
        if (demand > ceiling) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}

template <typename Row, CogKind K>
inline constexpr bool row_fits_cog_v = evaluate_row_fits_cog<Row, K>();

}  // namespace detail

// FitsCog answers "could any Cog of this kind run this row". The
// runtime helper below answers "can this one measured Cog run it".
// Neither replaces the other.
template <typename Row, CogKind K>
concept FitsCog = effects::IsConcurrentRow<Row> && HasCogCapacity<K> && detail::row_fits_cog_v<Row, K>;

namespace detail {

// The per-axis mapping onto schema fields lives next to the ceiling
// table so both move together.
template <CogKind K>
struct caps_runtime_capacity;

template <>
struct caps_runtime_capacity<CogKind::Gpu> {
    [[nodiscard]] static constexpr std::uint64_t for_kind(GpuTargetCaps const& caps,
                                                          effects::ResourceKind axis) noexcept {
        using effects::ResourceKind;
        switch (axis) {
            case ResourceKind::Sm:
                return std::uint64_t{caps.sm_count.value()};
            case ResourceKind::WarpScheduler:
                return std::uint64_t{caps.sm_count.value()} * std::uint64_t{caps.warp_schedulers_per_sm.value()};
            case ResourceKind::RegistersPerWarp:
                return std::uint64_t{caps.max_regs_per_thread.value()} * std::uint64_t{caps.warp_size.value()};
            case ResourceKind::Smem:
                return std::uint64_t{caps.sm_count.value()} * std::uint64_t{caps.smem_per_sm_bytes.value()};
            case ResourceKind::L2:
                return caps.l2_bytes.value();
            case ResourceKind::HbmBytes:
                return caps.hbm_bytes.value();
            case ResourceKind::HbmBw:
                return caps.hbm_bandwidth_bytes_per_sec.value();
            case ResourceKind::NvlinkBw:
                return caps.nvlink_bandwidth_bytes_per_sec.value();
            // No schema field carries PCIe bandwidth, so the axis
            // reads 0 and rejects every demand, matching the ceiling.
            case ResourceKind::PcieBw:
                return 0ULL;
            case ResourceKind::PowerWatts:
                return std::uint64_t{caps.tdp_watts.value()};
            case ResourceKind::ThermalCelsius:
                return std::uint64_t{caps.thermal_throttle_celsius.value()};
            default:
                return 0ULL;
        }
    }
};

template <>
struct caps_runtime_capacity<CogKind::NicPort> {
    [[nodiscard]] static constexpr std::uint64_t for_kind(NicPortTargetCaps const& caps,
                                                          effects::ResourceKind axis) noexcept {
        using effects::ResourceKind;
        switch (axis) {
            case ResourceKind::NicQ:
                return std::uint64_t{caps.max_tx_queues.value()} + std::uint64_t{caps.max_rx_queues.value()};
            case ResourceKind::NicQp:
                return std::uint64_t{caps.max_qp_count.value()};
            case ResourceKind::NicCq:
                return std::uint64_t{caps.max_cq_count.value()};
            case ResourceKind::NicMr:
                return std::uint64_t{caps.max_mr_count.value()};
            case ResourceKind::Tcam:
                return std::uint64_t{caps.tcam_entries.value()};
            case ResourceKind::PcieBw:
                return 0ULL;
            default:
                return 0ULL;
        }
    }
};

template <>
struct caps_runtime_capacity<CogKind::NvSwitch> {
    [[nodiscard]] static constexpr std::uint64_t for_kind(NvSwitchTargetCaps const& caps,
                                                          effects::ResourceKind axis) noexcept {
        using effects::ResourceKind;
        switch (axis) {
            case ResourceKind::SwitchEgressBw:
                return caps.aggregate_bandwidth_bytes_per_sec.value();
            case ResourceKind::SwitchBuffer:
                return caps.buffer_bytes.value();
            case ResourceKind::Tcam:
                return std::uint64_t{caps.tcam_entries.value()};
            default:
                return 0ULL;
        }
    }
};

template <>
struct caps_runtime_capacity<CogKind::CpuCore> {
    [[nodiscard]] static constexpr std::uint64_t for_kind(CpuCoreTargetCaps const& caps,
                                                          effects::ResourceKind axis) noexcept {
        using effects::ResourceKind;
        switch (axis) {
            case ResourceKind::CpuCore:
                return 1ULL;
            case ResourceKind::L2:
                return std::uint64_t{caps.l2_bytes.value()};
            default:
                return 0ULL;
        }
    }
};

template <>
struct caps_runtime_capacity<CogKind::CpuSocket> {
    [[nodiscard]] static constexpr std::uint64_t for_kind(CpuSocketTargetCaps const& caps,
                                                          effects::ResourceKind axis) noexcept {
        using effects::ResourceKind;
        switch (axis) {
            case ResourceKind::CpuCore:
                return std::uint64_t{caps.core_count.value()};
            case ResourceKind::Llc:
                return caps.l3_bytes.value();
            case ResourceKind::PowerWatts:
                return std::uint64_t{caps.tdp_watts.value()};
            case ResourceKind::ThermalCelsius:
                return std::uint64_t{caps.thermal_throttle_celsius.value()};
            default:
                return 0ULL;
        }
    }
};

template <>
struct caps_runtime_capacity<CogKind::DramChannel> {
    [[nodiscard]] static constexpr std::uint64_t for_kind(DramChannelTargetCaps const& /*caps*/,
                                                          effects::ResourceKind /*axis*/) noexcept {
        return 0ULL;
    }
};

}  // namespace detail

template <typename Row, CogKind K>
    requires effects::IsConcurrentRow<Row> && HasCogCapacity<K>
[[nodiscard]] constexpr bool fits_cog_caps_runtime(caps_for_t<K> const& caps) noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^effects::ResourceKind));
    bool fits = true;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        constexpr effects::ResourceKind axis = [:en:];
        constexpr std::uint64_t demand = effects::concurrent_row_value_v<axis, Row>;
        if (demand > 0) {
            const std::uint64_t capacity = detail::caps_runtime_capacity<K>::for_kind(caps, axis);
            if (demand > capacity) {
                fits = false;
            }
        }
    }
#pragma GCC diagnostic pop
    return fits;
}

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
inline constexpr bool has_caps_runtime_capacity_v = requires(caps_for_t<K> const& caps, effects::ResourceKind axis) {
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
static_assert(cog_max_capacity<CogKind::Gpu>::for_kind(effects::ResourceKind::Sm) > 0);
static_assert(cog_max_capacity<CogKind::Gpu>::for_kind(effects::ResourceKind::HbmBytes) > 0);
static_assert(cog_max_capacity<CogKind::CpuCore>::for_kind(effects::ResourceKind::CpuCore) > 0);
static_assert(cog_max_capacity<CogKind::CpuSocket>::for_kind(effects::ResourceKind::CpuCore) > 0);
static_assert(cog_max_capacity<CogKind::NicPort>::for_kind(effects::ResourceKind::NicQp) > 0);
static_assert(cog_max_capacity<CogKind::NvSwitch>::for_kind(effects::ResourceKind::SwitchEgressBw) > 0);

static_assert(cog_max_capacity<CogKind::Gpu>::for_kind(effects::ResourceKind::NicQp) == 0);
static_assert(cog_max_capacity<CogKind::Gpu>::for_kind(effects::ResourceKind::SwitchEgressBw) == 0);
static_assert(cog_max_capacity<CogKind::Gpu>::for_kind(effects::ResourceKind::CpuCore) == 0);
static_assert(cog_max_capacity<CogKind::NicPort>::for_kind(effects::ResourceKind::Sm) == 0);
static_assert(cog_max_capacity<CogKind::NicPort>::for_kind(effects::ResourceKind::HbmBytes) == 0);
static_assert(cog_max_capacity<CogKind::NvSwitch>::for_kind(effects::ResourceKind::Sm) == 0);
static_assert(cog_max_capacity<CogKind::NvSwitch>::for_kind(effects::ResourceKind::NicQp) == 0);
static_assert(cog_max_capacity<CogKind::CpuCore>::for_kind(effects::ResourceKind::Sm) == 0);
static_assert(cog_max_capacity<CogKind::CpuCore>::for_kind(effects::ResourceKind::NicQp) == 0);

using H100ComputeRow = effects::ConcurrentRow<effects::SmBudget<132>, effects::HbmBytes<80000000000ULL>>;
static_assert(FitsCog<H100ComputeRow, CogKind::Gpu>);

using NicAllReduceRow = effects::ConcurrentRow<effects::NicQp<4>, effects::NicCq<4>, effects::NicMr<8>>;
static_assert(FitsCog<NicAllReduceRow, CogKind::NicPort>);

using SwitchRow =
    effects::ConcurrentRow<effects::SwitchEgressBw<400000000000ULL>, effects::SwitchBufferCells<32 * 1024>>;
static_assert(FitsCog<SwitchRow, CogKind::NvSwitch>);

using SocketRow = effects::ConcurrentRow<effects::CpuCoreBudget<64>, effects::LlcBytes<128 * 1024 * 1024>>;
static_assert(FitsCog<SocketRow, CogKind::CpuSocket>);

// A row that asks for nothing satisfies every axis vacuously.
static_assert(FitsCog<effects::ConcurrentRow<>, CogKind::Gpu>);
static_assert(FitsCog<effects::ConcurrentRow<>, CogKind::NicPort>);
static_assert(FitsCog<effects::ConcurrentRow<>, CogKind::NvSwitch>);

using OversubscribedSmRow = effects::ConcurrentRow<effects::SmBudget<999>>;
static_assert(!FitsCog<OversubscribedSmRow, CogKind::Gpu>);

using OversubscribedHbmRow = effects::ConcurrentRow<effects::HbmBytes<512ULL * 1024 * 1024 * 1024>>;
static_assert(!FitsCog<OversubscribedHbmRow, CogKind::Gpu>);

using ConcurrentOverSubRow = effects::concurrent_row_sum_t<effects::ConcurrentRow<effects::SmBudget<200>>,
                                                           effects::ConcurrentRow<effects::SmBudget<200>>>;
static_assert(!FitsCog<ConcurrentOverSubRow, CogKind::Gpu>);
static_assert(effects::concurrent_row_value_v<effects::ResourceKind::Sm, ConcurrentOverSubRow> == 400);

// A demand on an axis the kind does not expose fails, because that
// axis has a ceiling of zero.
using NicDemandOnGpu = effects::ConcurrentRow<effects::NicQp<4>>;
static_assert(!FitsCog<NicDemandOnGpu, CogKind::Gpu>);

using GpuDemandOnNic = effects::ConcurrentRow<effects::SmBudget<4>>;
static_assert(!FitsCog<GpuDemandOnNic, CogKind::NicPort>);

using SwitchDemandOnCpu = effects::ConcurrentRow<effects::SwitchEgressBw<100000000000ULL>>;
static_assert(!FitsCog<SwitchDemandOnCpu, CogKind::CpuSocket>);

static_assert(!FitsCog<H100ComputeRow, CogKind::PsuRail>);
static_assert(!FitsCog<H100ComputeRow, CogKind::BmcSensor>);
static_assert(!FitsCog<H100ComputeRow, CogKind::OpticalTransceiver>);
static_assert(!FitsCog<effects::ConcurrentRow<>, CogKind::PsuRail>);

// A single budget tag is not a row, and neither is a plain type.
static_assert(!FitsCog<int, CogKind::Gpu>);
static_assert(!FitsCog<effects::resource::SmBudget<32>, CogKind::Gpu>);

// A row that exactly saturates a ceiling still admits, because some
// Cog of that kind has exactly that capacity.
using SaturateGpuSm = effects::ConcurrentRow<effects::SmBudget<320>>;
static_assert(FitsCog<SaturateGpuSm, CogKind::Gpu>);

using SaturateGpuHbm = effects::ConcurrentRow<effects::HbmBytes<384ULL * 1024 * 1024 * 1024>>;
static_assert(FitsCog<SaturateGpuHbm, CogKind::Gpu>);

using OneOverGpuSm = effects::ConcurrentRow<effects::SmBudget<321>>;
static_assert(!FitsCog<OneOverGpuSm, CogKind::Gpu>);

}  // namespace detail::fits_cog_self_test

}  // namespace crucible::cog
