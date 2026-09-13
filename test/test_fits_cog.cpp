// Including the header here puts its static_asserts into the build
// graph.  Every constexpr accessor below is driven with non-constant
// arguments, so a consteval-versus-constexpr slip fails at runtime
// rather than passing silently.

#include <crucible/cog/CogIdentity.h>
#include <crucible/cog/FitsCog.h>
#include <crucible/cog/TargetCaps.h>
#include <crucible/effects/Concurrent.h>
#include <crucible/effects/Resources.h>
#include <crucible/effects/EffectRow.h>

#include <cassert>
#include <cstdint>
#include <cstdio>

namespace cog = crucible::cog;
namespace effects = crucible::effects;

static void test_has_cog_capacity_substrate_admit() {
    static_assert(cog::HasCogCapacity<cog::CogKind::Gpu>);
    static_assert(cog::HasCogCapacity<cog::CogKind::CpuCore>);
    static_assert(cog::HasCogCapacity<cog::CogKind::CpuSocket>);
    static_assert(cog::HasCogCapacity<cog::CogKind::NicPort>);
    static_assert(cog::HasCogCapacity<cog::CogKind::NvSwitch>);
    static_assert(cog::HasCogCapacity<cog::CogKind::DramChannel>);
}

static void test_has_cog_capacity_non_substrate_reject() {
    static_assert(!cog::HasCogCapacity<cog::CogKind::PsuRail>);
    static_assert(!cog::HasCogCapacity<cog::CogKind::BmcSensor>);
    static_assert(!cog::HasCogCapacity<cog::CogKind::OpticalTransceiver>);
    static_assert(!cog::HasCogCapacity<cog::CogKind::NvmeNamespace>);
    static_assert(!cog::HasCogCapacity<cog::CogKind::PcieLaneGroup>);
    static_assert(!cog::HasCogCapacity<cog::CogKind::Datacenter>);
    static_assert(!cog::HasCogCapacity<cog::CogKind::Rack>);
    static_assert(!cog::HasCogCapacity<cog::CogKind::Server>);
    static_assert(!cog::HasCogCapacity<cog::CogKind::GpuPackage>);
}

static void test_gpu_ceilings_runtime() {
    using cog::cog_max_capacity;
    using cog::CogKind;
    using effects::ResourceKind;

    volatile auto axis = ResourceKind::Sm;
    auto sm = cog_max_capacity<CogKind::Gpu>::for_kind(const_cast<ResourceKind&>(axis));
    assert(sm == 320ULL);

    axis = ResourceKind::HbmBytes;
    auto hbm = cog_max_capacity<CogKind::Gpu>::for_kind(const_cast<ResourceKind&>(axis));
    // The ceiling clears the largest shipped HBM3E part with headroom.
    // A ceiling below real silicon would reject rows that do fit.
    assert(hbm == 384ULL * 1024 * 1024 * 1024);

    // PcieBw has no ceiling of its own, so both layers report 0 and
    // reject any PcieBw demand.
    axis = ResourceKind::PcieBw;
    auto pcie_on_gpu = cog_max_capacity<CogKind::Gpu>::for_kind(const_cast<ResourceKind&>(axis));
    assert(pcie_on_gpu == 0ULL);

    // An axis the substrate does not expose reads as 0.
    axis = ResourceKind::NicQp;
    auto nic_on_gpu = cog_max_capacity<CogKind::Gpu>::for_kind(const_cast<ResourceKind&>(axis));
    assert(nic_on_gpu == 0ULL);

    axis = ResourceKind::Tcam;
    auto tcam_on_gpu = cog_max_capacity<CogKind::Gpu>::for_kind(const_cast<ResourceKind&>(axis));
    assert(tcam_on_gpu == 0ULL);
}

static void test_nic_ceilings_runtime() {
    using cog::cog_max_capacity;
    using cog::CogKind;
    using effects::ResourceKind;

    volatile auto axis = ResourceKind::NicQp;
    auto qp = cog_max_capacity<CogKind::NicPort>::for_kind(const_cast<ResourceKind&>(axis));
    assert(qp == 16ULL * 1024 * 1024);

    axis = ResourceKind::Sm;
    auto sm_on_nic = cog_max_capacity<CogKind::NicPort>::for_kind(const_cast<ResourceKind&>(axis));
    assert(sm_on_nic == 0ULL);
}

static void test_fits_cog_h100_compute_row() {
    using H100Row = effects::ConcurrentRow<effects::SmBudget<132>, effects::HbmBytes<80000000000ULL>>;
    static_assert(cog::FitsCog<H100Row, cog::CogKind::Gpu>);
    static_assert(!cog::FitsCog<H100Row, cog::CogKind::NicPort>);
    static_assert(!cog::FitsCog<H100Row, cog::CogKind::NvSwitch>);
    static_assert(!cog::FitsCog<H100Row, cog::CogKind::PsuRail>);
}

static void test_fits_cog_oversubscription_rejection() {
    // GPU: 999 SMs > 320 ceiling
    using GpuOver = effects::ConcurrentRow<effects::SmBudget<999>>;
    static_assert(!cog::FitsCog<GpuOver, cog::CogKind::Gpu>);

    // NIC: 100M QPs > 16M ceiling
    using NicOver = effects::ConcurrentRow<effects::NicQp<100000000>>;
    static_assert(!cog::FitsCog<NicOver, cog::CogKind::NicPort>);

    // NvSwitch: 100 TB/s > 32 TB/s ceiling
    using SwitchOver = effects::ConcurrentRow<effects::SwitchEgressBw<100ULL * 1024 * 1024 * 1024 * 1024>>;
    static_assert(!cog::FitsCog<SwitchOver, cog::CogKind::NvSwitch>);

    // CPU socket: 999 cores > 256 ceiling
    using CpuOver = effects::ConcurrentRow<effects::CpuCoreBudget<999>>;
    static_assert(!cog::FitsCog<CpuOver, cog::CogKind::CpuSocket>);
}

// Axis isolation is what stops a NIC kernel binding to a GPU Cog.
static void test_fits_cog_axis_isolation() {
    using NicQpRow = effects::ConcurrentRow<effects::NicQp<4>>;
    static_assert(!cog::FitsCog<NicQpRow, cog::CogKind::Gpu>);
    static_assert(!cog::FitsCog<NicQpRow, cog::CogKind::NvSwitch>);
    static_assert(!cog::FitsCog<NicQpRow, cog::CogKind::CpuCore>);
    static_assert(cog::FitsCog<NicQpRow, cog::CogKind::NicPort>);

    using SmRow = effects::ConcurrentRow<effects::SmBudget<8>>;
    static_assert(cog::FitsCog<SmRow, cog::CogKind::Gpu>);
    static_assert(!cog::FitsCog<SmRow, cog::CogKind::NicPort>);
    static_assert(!cog::FitsCog<SmRow, cog::CogKind::CpuSocket>);
    static_assert(!cog::FitsCog<SmRow, cog::CogKind::NvSwitch>);
}

static void test_fits_cog_empty_row_admits() {
    using Empty = effects::ConcurrentRow<>;
    static_assert(cog::FitsCog<Empty, cog::CogKind::Gpu>);
    static_assert(cog::FitsCog<Empty, cog::CogKind::NicPort>);
    static_assert(cog::FitsCog<Empty, cog::CogKind::NvSwitch>);
    static_assert(cog::FitsCog<Empty, cog::CogKind::CpuCore>);
    static_assert(cog::FitsCog<Empty, cog::CogKind::CpuSocket>);
    static_assert(cog::FitsCog<Empty, cog::CogKind::DramChannel>);

    // The capacity gate fires before any axis comparison, so even an
    // empty row is rejected on a non-substrate kind.
    static_assert(!cog::FitsCog<Empty, cog::CogKind::PsuRail>);
}

static void test_fits_cog_shape_rejection() {
    static_assert(!cog::FitsCog<int, cog::CogKind::Gpu>);
    static_assert(!cog::FitsCog<float, cog::CogKind::Gpu>);
    static_assert(!cog::FitsCog<void, cog::CogKind::Gpu>);
    static_assert(!cog::FitsCog<effects::resource::SmBudget<32>, cog::CogKind::Gpu>);

    // effects::Row is a different row family, a set of effect atoms.
    static_assert(!cog::FitsCog<effects::Row<>, cog::CogKind::Gpu>);
    static_assert(!cog::FitsCog<effects::Row<effects::Effect::Bg>, cog::CogKind::Gpu>);
}

// FitsCog sees the already-summed row.
static void test_fits_cog_concurrent_sum_overflow() {
    using OpA = effects::ConcurrentRow<effects::SmBudget<200>>;
    using OpB = effects::ConcurrentRow<effects::SmBudget<200>>;
    using Combined = effects::concurrent_row_sum_t<OpA, OpB>;

    // Each individually fits (200 ≤ 320).
    static_assert(cog::FitsCog<OpA, cog::CogKind::Gpu>);
    static_assert(cog::FitsCog<OpB, cog::CogKind::Gpu>);

    // Combined does NOT fit (200 + 200 = 400 > 320).
    static_assert(effects::concurrent_row_value_v<effects::ResourceKind::Sm, Combined> == 400);
    static_assert(!cog::FitsCog<Combined, cog::CogKind::Gpu>);
}

static void test_fits_cog_boundary_saturate() {
    // The comparison is strict, so demand equal to the ceiling admits.
    using Sat = effects::ConcurrentRow<effects::SmBudget<320>, effects::HbmBytes<384ULL * 1024 * 1024 * 1024>>;
    static_assert(cog::FitsCog<Sat, cog::CogKind::Gpu>);

    using OneOverSm = effects::ConcurrentRow<effects::SmBudget<321>, effects::HbmBytes<384ULL * 1024 * 1024 * 1024>>;
    static_assert(!cog::FitsCog<OneOverSm, cog::CogKind::Gpu>);

    using OneOverHbm =
        effects::ConcurrentRow<effects::SmBudget<320>, effects::HbmBytes<384ULL * 1024 * 1024 * 1024 + 1>>;
    static_assert(!cog::FitsCog<OneOverHbm, cog::CogKind::Gpu>);

    // A shipped 288 GB part must admit on a GPU Cog.  This is the
    // regression witness for the ceiling.
    using MI325XHbm = effects::ConcurrentRow<effects::HbmBytes<288ULL * 1024 * 1024 * 1024>>;
    static_assert(cog::FitsCog<MI325XHbm, cog::CogKind::Gpu>);
}

static void test_fits_cog_caps_runtime_h100() {
    namespace safety = crucible::safety;
    cog::GpuTargetCaps h100{};
    // The vendor-tagged fields need explicit construction at each
    // assignment site.
    h100.sm_count = safety::Tagged<std::uint16_t, safety::source::Vendor>{std::uint16_t{132}};  // H100 SXM5
    h100.warp_schedulers_per_sm = safety::Tagged<std::uint16_t, safety::source::Vendor>{std::uint16_t{4}};
    h100.smem_per_sm_bytes = safety::Tagged<std::uint32_t, safety::source::Vendor>{std::uint32_t{228 * 1024}};
    h100.l2_bytes = safety::Tagged<std::uint64_t, safety::source::Vendor>{50ULL * 1024 * 1024};
    h100.hbm_bytes = safety::Tagged<std::uint64_t, safety::source::Vendor>{80ULL * 1024 * 1024 * 1024};
    h100.hbm_bandwidth_bytes_per_sec =
        safety::Tagged<std::uint64_t, safety::source::Vendor>{3350ULL * 1024 * 1024 * 1024};
    h100.nvlink_bandwidth_bytes_per_sec =
        safety::Tagged<std::uint64_t, safety::source::Vendor>{900ULL * 1024 * 1024 * 1024};
    h100.tdp_watts = safety::Tagged<std::uint16_t, safety::source::Vendor>{std::uint16_t{700}};
    h100.thermal_throttle_celsius = safety::Tagged<std::uint16_t, safety::source::Vendor>{std::uint16_t{85}};

    using Fits = effects::ConcurrentRow<effects::SmBudget<128>, effects::HbmBytes<70000000000ULL>>;
    volatile bool fits = cog::fits_cog_caps_runtime<Fits, cog::CogKind::Gpu>(h100);
    assert(fits);

    // 200 SMs is over H100's 132, but under the family ceiling 320 —
    // FitsCog (compile-time) admits, fits_cog_caps_runtime rejects.
    using OverSm = effects::ConcurrentRow<effects::SmBudget<200>>;
    static_assert(cog::FitsCog<OverSm, cog::CogKind::Gpu>);
    volatile bool runtime_over = cog::fits_cog_caps_runtime<OverSm, cog::CogKind::Gpu>(h100);
    assert(!runtime_over);

    // HBM > h100's 80 GB but under family ceiling 384 GB —
    // same compile-time-admits / runtime-rejects pattern.
    using OverHbm = effects::ConcurrentRow<effects::HbmBytes<200ULL * 1024 * 1024 * 1024>>;
    static_assert(cog::FitsCog<OverHbm, cog::CogKind::Gpu>);
    volatile bool runtime_hbm = cog::fits_cog_caps_runtime<OverHbm, cog::CogKind::Gpu>(h100);
    assert(!runtime_hbm);
}

static void test_fits_cog_caps_runtime_nic() {
    namespace safety = crucible::safety;
    cog::NicPortTargetCaps nic{};
    nic.line_rate_bytes_per_sec = safety::Tagged<std::uint64_t, safety::source::Vendor>{50ULL * 1024 * 1024 * 1024};
    nic.max_qp_count = safety::Tagged<std::uint32_t, safety::source::Vendor>{std::uint32_t{1024}};
    nic.max_cq_count = safety::Tagged<std::uint32_t, safety::source::Vendor>{std::uint32_t{1024}};
    nic.max_mr_count = safety::Tagged<std::uint32_t, safety::source::Vendor>{std::uint32_t{64}};

    using Fits = effects::ConcurrentRow<effects::NicQp<512>, effects::NicCq<512>, effects::NicMr<32>>;
    volatile bool fits = cog::fits_cog_caps_runtime<Fits, cog::CogKind::NicPort>(nic);
    assert(fits);

    using OverQp = effects::ConcurrentRow<effects::NicQp<2048>>;
    volatile bool over_qp = cog::fits_cog_caps_runtime<OverQp, cog::CogKind::NicPort>(nic);
    assert(!over_qp);
}

// A substrate added to one binding table but forgotten in the other
// breaks the runtime caps path.  Both directions are pinned.
static void test_has_cog_capacity_caps_lockstep() {
    static_assert(cog::HasCaps<cog::CogKind::Gpu> == cog::HasCogCapacity<cog::CogKind::Gpu>);
    static_assert(cog::HasCaps<cog::CogKind::CpuCore> == cog::HasCogCapacity<cog::CogKind::CpuCore>);
    static_assert(cog::HasCaps<cog::CogKind::CpuSocket> == cog::HasCogCapacity<cog::CogKind::CpuSocket>);
    static_assert(cog::HasCaps<cog::CogKind::NicPort> == cog::HasCogCapacity<cog::CogKind::NicPort>);
    static_assert(cog::HasCaps<cog::CogKind::NvSwitch> == cog::HasCogCapacity<cog::CogKind::NvSwitch>);
    static_assert(cog::HasCaps<cog::CogKind::DramChannel> == cog::HasCogCapacity<cog::CogKind::DramChannel>);

    static_assert(cog::HasCaps<cog::CogKind::PsuRail> == cog::HasCogCapacity<cog::CogKind::PsuRail>);
    static_assert(cog::HasCaps<cog::CogKind::BmcSensor> == cog::HasCogCapacity<cog::CogKind::BmcSensor>);
}

// A row fits only if every axis fits.
static void test_fits_cog_multi_axis_all_must_fit() {
    using AllFit = effects::ConcurrentRow<effects::SmBudget<128>, effects::HbmBytes<80000000000ULL>,
                                          effects::HbmBandwidth<3000000000000ULL>>;
    static_assert(cog::FitsCog<AllFit, cog::CogKind::Gpu>);

    // Sm fits, HBM fits, but bandwidth is way over (10 TB/s > 9 TB/s).
    using OneAxisOver = effects::ConcurrentRow<effects::SmBudget<128>, effects::HbmBytes<80000000000ULL>,
                                               effects::HbmBandwidth<10ULL * 1024 * 1024 * 1024 * 1024>>;
    static_assert(!cog::FitsCog<OneAxisOver, cog::CogKind::Gpu>);

    using HbmOver = effects::ConcurrentRow<effects::SmBudget<128>, effects::HbmBytes<512ULL * 1024 * 1024 * 1024>,
                                           effects::HbmBandwidth<3000000000000ULL>>;
    static_assert(!cog::FitsCog<HbmOver, cog::CogKind::Gpu>);
}

// Every substrate is driven through every axis, so no switch path
// reaches an unreachable arm.
static void test_every_substrate_every_axis_runtime() {
    using effects::ResourceKind;
    using cog::cog_max_capacity;
    using cog::CogKind;

    // 23 is the number of ResourceKind atoms.
    constexpr int N_AXES = 23;
    int gpu_nonzero = 0, nic_nonzero = 0, sw_nonzero = 0;
    int cpu_nonzero = 0, sock_nonzero = 0;

    for (int i = 0; i < N_AXES; ++i) {
        volatile auto axis = static_cast<ResourceKind>(static_cast<std::uint8_t>(i));
        auto v_gpu = cog_max_capacity<CogKind::Gpu>::for_kind(const_cast<ResourceKind&>(axis));
        auto v_nic = cog_max_capacity<CogKind::NicPort>::for_kind(const_cast<ResourceKind&>(axis));
        auto v_sw = cog_max_capacity<CogKind::NvSwitch>::for_kind(const_cast<ResourceKind&>(axis));
        auto v_cpu = cog_max_capacity<CogKind::CpuCore>::for_kind(const_cast<ResourceKind&>(axis));
        auto v_sock = cog_max_capacity<CogKind::CpuSocket>::for_kind(const_cast<ResourceKind&>(axis));
        if (v_gpu > 0) ++gpu_nonzero;
        if (v_nic > 0) ++nic_nonzero;
        if (v_sw > 0) ++sw_nonzero;
        if (v_cpu > 0) ++cpu_nonzero;
        if (v_sock > 0) ++sock_nonzero;
    }
    assert(gpu_nonzero > 0);
    assert(nic_nonzero > 0);
    assert(sw_nonzero > 0);
    assert(cpu_nonzero > 0);
    assert(sock_nonzero > 0);
    // GPU exposes the most axes (compute + memory + power).
    assert(gpu_nonzero >= nic_nonzero);
}

int main() {
    test_has_cog_capacity_substrate_admit();
    test_has_cog_capacity_non_substrate_reject();
    test_gpu_ceilings_runtime();
    test_nic_ceilings_runtime();
    test_fits_cog_h100_compute_row();
    test_fits_cog_oversubscription_rejection();
    test_fits_cog_axis_isolation();
    test_fits_cog_empty_row_admits();
    test_fits_cog_shape_rejection();
    test_fits_cog_concurrent_sum_overflow();
    test_fits_cog_boundary_saturate();
    test_fits_cog_caps_runtime_h100();
    test_fits_cog_caps_runtime_nic();
    test_has_cog_capacity_caps_lockstep();
    test_fits_cog_multi_axis_all_must_fit();
    test_every_substrate_every_axis_runtime();
    std::puts("FitsCog: all 16 sentinel groups passed.");
    return 0;
}
