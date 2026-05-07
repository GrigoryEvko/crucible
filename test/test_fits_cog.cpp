// ── Sentinel TU for cog/FitsCog.h ────────────────────────────────────
//
// GAPS-191 ships a header-only static_assert block.  Per the
// "Header-only static_assert blind spot" feedback memory, headers
// shipped with embedded static_asserts aren't verified under project
// warning flags (-Wshadow / -Wconversion / -Werror=missing-noreturn /
// the GCC 16 "expansion-statement local non-static-constexpr" gotcha)
// unless a .cpp TU actually includes them.  This file is that TU.
//
// In addition to passively triggering compilation, every constexpr
// accessor is exercised with NON-CONSTANT runtime arguments via
// volatile barriers — per
// `feedback_algebra_runtime_smoke_test_discipline`.  Pure
// static_assert-only tests can mask consteval-vs-constexpr bugs (e.g.
// a function shipped consteval that smoke-tests build but cannot be
// called from runtime code) and SFINAE / inline-body bugs in the
// generic algorithms.

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

// ── Group 1: HasCogCapacity concept gate ───────────────────────────
//
// The concept distinguishes substrate from non-substrate CogKind atoms.
// Verified at compile time; here we additionally instantiate code paths
// that depend on the concept under runtime conditions.
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

// ── Group 2: cog_max_capacity per-axis ceilings ────────────────────
//
// Drive the consteval for_kind() accessor with NON-constant runtime
// arguments through volatile barriers, validating the runtime
// constexpr path.  A bug in the constexpr-vs-consteval split (e.g.,
// accidentally consteval-ing for_kind) would surface here.
static void test_gpu_ceilings_runtime() {
    using cog::cog_max_capacity;
    using cog::CogKind;
    using effects::ResourceKind;

    volatile auto axis = ResourceKind::Sm;
    auto sm = cog_max_capacity<CogKind::Gpu>::for_kind(
        const_cast<ResourceKind&>(axis));
    assert(sm == 320ULL);

    axis = ResourceKind::HbmBytes;
    auto hbm = cog_max_capacity<CogKind::Gpu>::for_kind(
        const_cast<ResourceKind&>(axis));
    // Audit follow-up (2026-05-07): bumped 256GB → 384GB to cover
    // MI325X's 288GB HBM3E + headroom for next-gen.  Setting
    // ceiling lower than the largest shipped chip would falsely
    // reject Rows that fit real silicon.
    assert(hbm == 384ULL * 1024 * 1024 * 1024);

    // PcieBw deliberately = 0 in the compile-time ceiling — the
    // runtime helper also returns 0 (no derived field yet in
    // TargetCaps).  Both layers reject PcieBw demand consistently
    // until cog/Calibrate.h ships pcie_bw_bytes_per_sec.
    axis = ResourceKind::PcieBw;
    auto pcie_on_gpu = cog_max_capacity<CogKind::Gpu>::for_kind(
        const_cast<ResourceKind&>(axis));
    assert(pcie_on_gpu == 0ULL);

    // Axes the substrate doesn't expose return 0.
    axis = ResourceKind::NicQp;
    auto nic_on_gpu = cog_max_capacity<CogKind::Gpu>::for_kind(
        const_cast<ResourceKind&>(axis));
    assert(nic_on_gpu == 0ULL);

    axis = ResourceKind::Tcam;
    auto tcam_on_gpu = cog_max_capacity<CogKind::Gpu>::for_kind(
        const_cast<ResourceKind&>(axis));
    assert(tcam_on_gpu == 0ULL);
}

static void test_nic_ceilings_runtime() {
    using cog::cog_max_capacity;
    using cog::CogKind;
    using effects::ResourceKind;

    volatile auto axis = ResourceKind::NicQp;
    auto qp = cog_max_capacity<CogKind::NicPort>::for_kind(
        const_cast<ResourceKind&>(axis));
    assert(qp == 16ULL * 1024 * 1024);

    axis = ResourceKind::Sm;
    auto sm_on_nic = cog_max_capacity<CogKind::NicPort>::for_kind(
        const_cast<ResourceKind&>(axis));
    assert(sm_on_nic == 0ULL);
}

// ── Group 3: FitsCog admission with realistic Hopper row ──────────
static void test_fits_cog_h100_compute_row() {
    using H100Row = effects::ConcurrentRow<
        effects::SmBudget<132>,
        effects::HbmBytes<80'000'000'000ULL>>;
    static_assert(cog::FitsCog<H100Row, cog::CogKind::Gpu>);
    static_assert(!cog::FitsCog<H100Row, cog::CogKind::NicPort>);
    static_assert(!cog::FitsCog<H100Row, cog::CogKind::NvSwitch>);
    static_assert(!cog::FitsCog<H100Row, cog::CogKind::PsuRail>);
}

// ── Group 4: FitsCog rejects oversubscription on each substrate ───
static void test_fits_cog_oversubscription_rejection() {
    // GPU: 999 SMs > 320 ceiling
    using GpuOver = effects::ConcurrentRow<effects::SmBudget<999>>;
    static_assert(!cog::FitsCog<GpuOver, cog::CogKind::Gpu>);

    // NIC: 100M QPs > 16M ceiling
    using NicOver = effects::ConcurrentRow<effects::NicQp<100'000'000>>;
    static_assert(!cog::FitsCog<NicOver, cog::CogKind::NicPort>);

    // NvSwitch: 100 TB/s > 32 TB/s ceiling
    using SwitchOver = effects::ConcurrentRow<
        effects::SwitchEgressBw<100ULL * 1024 * 1024 * 1024 * 1024>>;
    static_assert(!cog::FitsCog<SwitchOver, cog::CogKind::NvSwitch>);

    // CPU socket: 999 cores > 256 ceiling
    using CpuOver = effects::ConcurrentRow<effects::CpuCoreBudget<999>>;
    static_assert(!cog::FitsCog<CpuOver, cog::CogKind::CpuSocket>);
}

// ── Group 5: Cross-substrate axis-mismatch rejection ──────────────
//
// Row demanding axis X on Cog kind that doesn't expose X — rejected.
// This is the "axis isolation" semantic that prevents a NIC kernel
// from being mis-bound to a GPU Cog or vice versa.
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

// ── Group 6: Empty row admits any substrate ────────────────────────
static void test_fits_cog_empty_row_admits() {
    using Empty = effects::ConcurrentRow<>;
    static_assert(cog::FitsCog<Empty, cog::CogKind::Gpu>);
    static_assert(cog::FitsCog<Empty, cog::CogKind::NicPort>);
    static_assert(cog::FitsCog<Empty, cog::CogKind::NvSwitch>);
    static_assert(cog::FitsCog<Empty, cog::CogKind::CpuCore>);
    static_assert(cog::FitsCog<Empty, cog::CogKind::CpuSocket>);
    static_assert(cog::FitsCog<Empty, cog::CogKind::DramChannel>);

    // BUT: empty row still rejected on non-substrate kinds (the
    // HasCogCapacity gate fires before any axis comparison).
    static_assert(!cog::FitsCog<Empty, cog::CogKind::PsuRail>);
}

// ── Group 7: FitsCog rejects non-IsConcurrentRow shapes ───────────
static void test_fits_cog_shape_rejection() {
    static_assert(!cog::FitsCog<int, cog::CogKind::Gpu>);
    static_assert(!cog::FitsCog<float, cog::CogKind::Gpu>);
    static_assert(!cog::FitsCog<void, cog::CogKind::Gpu>);
    static_assert(!cog::FitsCog<effects::resource::SmBudget<32>, cog::CogKind::Gpu>);

    // effects::Row<Es...> is a different row family (set-of-Effect-
    // atoms) — not a ConcurrentRow.  Rejected.
    static_assert(!cog::FitsCog<effects::Row<>, cog::CogKind::Gpu>);
    static_assert(!cog::FitsCog<effects::Row<effects::Effect::Bg>, cog::CogKind::Gpu>);
}

// ── Group 8: Concurrent-sum integration (GAPS-190 ↔ GAPS-191) ─────
//
// FitsCog operates on the ALREADY-SUMMED row.  This group witnesses
// the integration with GAPS-190's concurrent_row_sum_t — given two
// per-op rows that individually fit, their concurrent sum may NOT
// fit and FitsCog correctly catches the combined demand.
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

// ── Group 9: Boundary — demand == ceiling admits ──────────────────
static void test_fits_cog_boundary_saturate() {
    // Exact-saturate cases admit (comparison is strict >, not >=).
    // HbmBytes ceiling = 384 GB (post-audit, MI325X coverage).
    using Sat = effects::ConcurrentRow<
        effects::SmBudget<320>,
        effects::HbmBytes<384ULL * 1024 * 1024 * 1024>>;
    static_assert(cog::FitsCog<Sat, cog::CogKind::Gpu>);

    // One-over fails on ANY axis.
    using OneOverSm = effects::ConcurrentRow<
        effects::SmBudget<321>,
        effects::HbmBytes<384ULL * 1024 * 1024 * 1024>>;
    static_assert(!cog::FitsCog<OneOverSm, cog::CogKind::Gpu>);

    using OneOverHbm = effects::ConcurrentRow<
        effects::SmBudget<320>,
        effects::HbmBytes<384ULL * 1024 * 1024 * 1024 + 1>>;
    static_assert(!cog::FitsCog<OneOverHbm, cog::CogKind::Gpu>);

    // MI325X shipped 2024 with 288 GB HBM3E — must admit on
    // CogKind::Gpu.  This is the regression witness for the audit
    // fix that bumped GPU HbmBytes ceiling from 256GB → 384GB.
    // A 288GB Row failed the old (256GB) ceiling — the audit
    // tightens the spec to actual silicon, not paper "max + 25%
    // headroom" arithmetic that under-counted.
    using MI325XHbm = effects::ConcurrentRow<
        effects::HbmBytes<288ULL * 1024 * 1024 * 1024>>;
    static_assert(cog::FitsCog<MI325XHbm, cog::CogKind::Gpu>);
}

// ── Group 10: fits_cog_caps_runtime — runtime caps integration ────
//
// Build a realistic GpuTargetCaps mocking H100 vendor + calibrated
// fields, then check fits_cog_caps_runtime against rows that should
// (and should not) fit.  Drives the constexpr function with a
// runtime caps parameter — the actual production-side path that the
// GAPS-188 mimic/CogMimic.h factory and GAPS-810 partition optimizer
// will use.
static void test_fits_cog_caps_runtime_h100() {
    namespace safety = crucible::safety;
    cog::GpuTargetCaps h100{};
    // Vendor datasheet (Tagged<T, source::Vendor> requires explicit
    // construction at the assignment site).
    h100.sm_count =
        safety::Tagged<std::uint16_t, safety::source::Vendor>{std::uint16_t{132}};  // H100 SXM5
    h100.warp_schedulers_per_sm =
        safety::Tagged<std::uint16_t, safety::source::Vendor>{std::uint16_t{4}};
    h100.smem_per_sm_bytes =
        safety::Tagged<std::uint32_t, safety::source::Vendor>{std::uint32_t{228 * 1024}};
    h100.l2_bytes =
        safety::Tagged<std::uint64_t, safety::source::Vendor>{50ULL * 1024 * 1024};
    h100.hbm_bytes =
        safety::Tagged<std::uint64_t, safety::source::Vendor>{80ULL * 1024 * 1024 * 1024};
    h100.hbm_bandwidth_bytes_per_sec =
        safety::Tagged<std::uint64_t, safety::source::Vendor>{3'350ULL * 1024 * 1024 * 1024};
    h100.nvlink_bandwidth_bytes_per_sec =
        safety::Tagged<std::uint64_t, safety::source::Vendor>{900ULL * 1024 * 1024 * 1024};
    h100.tdp_watts =
        safety::Tagged<std::uint16_t, safety::source::Vendor>{std::uint16_t{700}};
    h100.thermal_throttle_celsius =
        safety::Tagged<std::uint16_t, safety::source::Vendor>{std::uint16_t{85}};

    using Fits = effects::ConcurrentRow<
        effects::SmBudget<128>,
        effects::HbmBytes<70'000'000'000ULL>>;
    volatile bool fits =
        cog::fits_cog_caps_runtime<Fits, cog::CogKind::Gpu>(h100);
    assert(fits);

    // 200 SMs is over H100's 132, but under the family ceiling 320 —
    // FitsCog (compile-time) admits, fits_cog_caps_runtime rejects.
    using OverSm = effects::ConcurrentRow<effects::SmBudget<200>>;
    static_assert(cog::FitsCog<OverSm, cog::CogKind::Gpu>);
    volatile bool runtime_over =
        cog::fits_cog_caps_runtime<OverSm, cog::CogKind::Gpu>(h100);
    assert(!runtime_over);

    // HBM > h100's 80 GB but under family ceiling 384 GB —
    // same compile-time-admits / runtime-rejects pattern.
    using OverHbm = effects::ConcurrentRow<
        effects::HbmBytes<200ULL * 1024 * 1024 * 1024>>;
    static_assert(cog::FitsCog<OverHbm, cog::CogKind::Gpu>);
    volatile bool runtime_hbm =
        cog::fits_cog_caps_runtime<OverHbm, cog::CogKind::Gpu>(h100);
    assert(!runtime_hbm);
}

static void test_fits_cog_caps_runtime_nic() {
    namespace safety = crucible::safety;
    cog::NicPortTargetCaps nic{};
    nic.line_rate_bytes_per_sec =
        safety::Tagged<std::uint64_t, safety::source::Vendor>{50ULL * 1024 * 1024 * 1024};
    nic.max_qp_count =
        safety::Tagged<std::uint32_t, safety::source::Vendor>{std::uint32_t{1024}};
    nic.max_cq_count =
        safety::Tagged<std::uint32_t, safety::source::Vendor>{std::uint32_t{1024}};
    nic.max_mr_count =
        safety::Tagged<std::uint32_t, safety::source::Vendor>{std::uint32_t{64}};

    using Fits = effects::ConcurrentRow<
        effects::NicQp<512>,
        effects::NicCq<512>,
        effects::NicMr<32>>;
    volatile bool fits =
        cog::fits_cog_caps_runtime<Fits, cog::CogKind::NicPort>(nic);
    assert(fits);

    using OverQp = effects::ConcurrentRow<effects::NicQp<2048>>;
    volatile bool over_qp =
        cog::fits_cog_caps_runtime<OverQp, cog::CogKind::NicPort>(nic);
    assert(!over_qp);
}

// ── Group 11: HasCogCapacity ↔ HasCaps consistency ────────────────
//
// The two binding tables must stay in lockstep.  A future substrate
// added to one but forgotten in the other breaks fits_cog_caps_runtime
// (which references caps_for_t<K>).  Pinned both directions.
static void test_has_cog_capacity_caps_lockstep() {
    static_assert(cog::HasCaps<cog::CogKind::Gpu> ==
                  cog::HasCogCapacity<cog::CogKind::Gpu>);
    static_assert(cog::HasCaps<cog::CogKind::CpuCore> ==
                  cog::HasCogCapacity<cog::CogKind::CpuCore>);
    static_assert(cog::HasCaps<cog::CogKind::CpuSocket> ==
                  cog::HasCogCapacity<cog::CogKind::CpuSocket>);
    static_assert(cog::HasCaps<cog::CogKind::NicPort> ==
                  cog::HasCogCapacity<cog::CogKind::NicPort>);
    static_assert(cog::HasCaps<cog::CogKind::NvSwitch> ==
                  cog::HasCogCapacity<cog::CogKind::NvSwitch>);
    static_assert(cog::HasCaps<cog::CogKind::DramChannel> ==
                  cog::HasCogCapacity<cog::CogKind::DramChannel>);

    // Non-substrate side
    static_assert(cog::HasCaps<cog::CogKind::PsuRail> ==
                  cog::HasCogCapacity<cog::CogKind::PsuRail>);
    static_assert(cog::HasCaps<cog::CogKind::BmcSensor> ==
                  cog::HasCogCapacity<cog::CogKind::BmcSensor>);
}

// ── Group 12: Cross-axis-fold soundness (multi-axis row) ──────────
//
// When a row carries multiple axes, ALL must fit — a single failing
// axis taints the whole row.  Verified by composition.
static void test_fits_cog_multi_axis_all_must_fit() {
    // All three axes within Gpu ceilings.
    using AllFit = effects::ConcurrentRow<
        effects::SmBudget<128>,
        effects::HbmBytes<80'000'000'000ULL>,
        effects::HbmBandwidth<3'000'000'000'000ULL>>;
    static_assert(cog::FitsCog<AllFit, cog::CogKind::Gpu>);

    // Sm fits, HBM fits, but bandwidth is way over (10 TB/s > 9 TB/s).
    using OneAxisOver = effects::ConcurrentRow<
        effects::SmBudget<128>,
        effects::HbmBytes<80'000'000'000ULL>,
        effects::HbmBandwidth<10ULL * 1024 * 1024 * 1024 * 1024>>;
    static_assert(!cog::FitsCog<OneAxisOver, cog::CogKind::Gpu>);

    // Sm OK, HBM way over.
    using HbmOver = effects::ConcurrentRow<
        effects::SmBudget<128>,
        effects::HbmBytes<512ULL * 1024 * 1024 * 1024>,
        effects::HbmBandwidth<3'000'000'000'000ULL>>;
    static_assert(!cog::FitsCog<HbmOver, cog::CogKind::Gpu>);
}

// ── Group 13: Per-Cog ceiling sanity at runtime ───────────────────
//
// Drive every shipped substrate through cog_max_capacity::for_kind
// for every ResourceKind atom at runtime — confirms no path through
// any switch hits UB / unreachable.
static void test_every_substrate_every_axis_runtime() {
    using effects::ResourceKind;
    using cog::cog_max_capacity;
    using cog::CogKind;

    // 23 ResourceKind atoms × 6 substrate CogKinds = 138 calls.
    // We just need the calls to return cleanly (no UB, no termination).
    constexpr int N_AXES = 23;
    int gpu_nonzero = 0, nic_nonzero = 0, sw_nonzero = 0;
    int cpu_nonzero = 0, sock_nonzero = 0;

    for (int i = 0; i < N_AXES; ++i) {
        volatile auto axis = static_cast<ResourceKind>(static_cast<std::uint8_t>(i));
        auto v_gpu = cog_max_capacity<CogKind::Gpu>::for_kind(
            const_cast<ResourceKind&>(axis));
        auto v_nic = cog_max_capacity<CogKind::NicPort>::for_kind(
            const_cast<ResourceKind&>(axis));
        auto v_sw = cog_max_capacity<CogKind::NvSwitch>::for_kind(
            const_cast<ResourceKind&>(axis));
        auto v_cpu = cog_max_capacity<CogKind::CpuCore>::for_kind(
            const_cast<ResourceKind&>(axis));
        auto v_sock = cog_max_capacity<CogKind::CpuSocket>::for_kind(
            const_cast<ResourceKind&>(axis));
        if (v_gpu > 0) ++gpu_nonzero;
        if (v_nic > 0) ++nic_nonzero;
        if (v_sw > 0) ++sw_nonzero;
        if (v_cpu > 0) ++cpu_nonzero;
        if (v_sock > 0) ++sock_nonzero;
    }
    // Every substrate exposes at least one budget axis.
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
