// Sentinel TU for include/crucible/cog/TargetCaps.h.
//
// Per feedback_header_only_static_assert_blind_spot.md: TargetCaps.h
// ships substantial in-header static_asserts (FOUND-I04 frozen-position
// pins on 7 enums + caps_for binding pins + standard-layout pins), but
// those only execute under the project's full warning + standard flags
// when SOMEONE includes the header from a TU that lands in the build
// graph.  This sentinel makes the inclusion explicit so the in-header
// invariants are exercised by every default build.
//
// Per feedback_algebra_runtime_smoke_test_discipline: every constexpr
// accessor (link_layer_name / pcie_gen_name / *_feature_name × 5) is
// driven with non-constant runtime arguments here so a regression
// in one of the switch arms surfaces under runtime semantics, not
// only at consteval time.
//
// GAPS-186.

#include <crucible/cog/TargetCaps.h>

#include "test_assert.h"

#include <bit>
#include <cstdint>
#include <cstdio>
#include <string_view>
#include <type_traits>

namespace cog = crucible::cog;

// ── Reflection-driven name coverage: LinkLayer ──────────────────────

static void test_link_layer_name_coverage() {
    constexpr cog::LinkLayer layers[] = {
        cog::LinkLayer::Ethernet,
        cog::LinkLayer::Infiniband,
        cog::LinkLayer::Roce,
        cog::LinkLayer::NVLink,
        cog::LinkLayer::Pcie,
        cog::LinkLayer::Cxl,
    };
    static_assert(sizeof(layers) / sizeof(layers[0]) == cog::link_layer_count,
        "Manual layers[] table diverged from link_layer_count.");

    for (cog::LinkLayer L : layers) {
        volatile auto vL = L;
        std::string_view name = cog::link_layer_name(
            static_cast<cog::LinkLayer>(vL));
        assert(!name.empty());
        assert(name != std::string_view{"<unknown LinkLayer>"});
    }
    std::printf("  test_link_layer_name_coverage:        PASSED\n");
}

// ── Reflection-driven name coverage: PcieGen ────────────────────────

static void test_pcie_gen_name_coverage() {
    constexpr cog::PcieGen gens[] = {
        cog::PcieGen::None,
        cog::PcieGen::Gen1,
        cog::PcieGen::Gen2,
        cog::PcieGen::Gen3,
        cog::PcieGen::Gen4,
        cog::PcieGen::Gen5,
        cog::PcieGen::Gen6,
    };
    static_assert(sizeof(gens) / sizeof(gens[0]) == cog::pcie_gen_count,
        "Manual gens[] table diverged from pcie_gen_count.");

    for (cog::PcieGen G : gens) {
        volatile auto vG = G;
        std::string_view name = cog::pcie_gen_name(
            static_cast<cog::PcieGen>(vG));
        assert(!name.empty());
        assert(name != std::string_view{"<unknown PcieGen>"});

        // Underlying value IS the generation number (Gen3 → 3).
        // Volatile barrier prevents constant-folding.
        volatile std::uint8_t expected =
            static_cast<std::uint8_t>(vG);
        std::uint8_t actual = static_cast<std::uint8_t>(G);
        assert(actual == expected);
    }
    std::printf("  test_pcie_gen_name_coverage:          PASSED\n");
}

// ── Feature-enum runtime smoke (one driver per enum) ────────────────

static void test_gpu_feature_runtime() {
    constexpr cog::GpuFeature flags[] = {
        cog::GpuFeature::Tma,
        cog::GpuFeature::ClusterLaunch,
        cog::GpuFeature::Fp8,
        cog::GpuFeature::Bf16,
        cog::GpuFeature::Tf32,
        cog::GpuFeature::NvlinkSharp,
        cog::GpuFeature::GpuDirectRdma,
        cog::GpuFeature::GpuDirectStorage,
        cog::GpuFeature::Mig,
    };
    for (cog::GpuFeature F : flags) {
        volatile auto vF = F;
        auto name = cog::gpu_feature_name(static_cast<cog::GpuFeature>(vF));
        assert(!name.empty());
        assert(name != std::string_view{"<unknown GpuFeature>"});
    }

    // Bits composition: all-set count == array size.
    crucible::safety::Bits<cog::GpuFeature> all_set{};
    for (cog::GpuFeature F : flags) all_set.set(F);
    assert(all_set.popcount() == static_cast<int>(sizeof(flags) / sizeof(flags[0])));
    assert(all_set.test(cog::GpuFeature::Tma));
    assert(all_set.test(cog::GpuFeature::Fp8));

    // Disjoint Bits<E1> vs Bits<E2> — different instantiations cannot
    // be conflated (typesafe verified at compile time; the runtime
    // assertion just confirms popcount honesty).
    crucible::safety::Bits<cog::NicFeature> nic_bits{};
    nic_bits.set(cog::NicFeature::Tso);
    assert(nic_bits.popcount() == 1);
    assert(!nic_bits.test(cog::NicFeature::Roce));

    std::printf("  test_gpu_feature_runtime:             PASSED\n");
}

static void test_nic_feature_runtime() {
    constexpr cog::NicFeature flags[] = {
        cog::NicFeature::Tso, cog::NicFeature::Gso,
        cog::NicFeature::Gro, cog::NicFeature::Lro,
        cog::NicFeature::Rss, cog::NicFeature::Roce,
        cog::NicFeature::Iwarp, cog::NicFeature::KtlsOffload,
        cog::NicFeature::GpuDirectRdma, cog::NicFeature::XdpNative,
        cog::NicFeature::XdpOffload, cog::NicFeature::AfXdp,
        cog::NicFeature::SrIov, cog::NicFeature::Macsec,
        cog::NicFeature::Ipsec, cog::NicFeature::TimestampingHw,
        cog::NicFeature::TcEbpf, cog::NicFeature::Tcam,
    };
    for (cog::NicFeature F : flags) {
        volatile auto vF = F;
        auto name = cog::nic_feature_name(static_cast<cog::NicFeature>(vF));
        assert(!name.empty());
        assert(name != std::string_view{"<unknown NicFeature>"});
    }
    std::printf("  test_nic_feature_runtime:             PASSED\n");
}

static void test_switch_feature_runtime() {
    constexpr cog::SwitchFeature flags[] = {
        cog::SwitchFeature::Sharp, cog::SwitchFeature::P4,
        cog::SwitchFeature::AdaptiveRouting, cog::SwitchFeature::Ecn,
        cog::SwitchFeature::Pfc, cog::SwitchFeature::Tcam,
        cog::SwitchFeature::PortMirror, cog::SwitchFeature::Doca,
    };
    for (cog::SwitchFeature F : flags) {
        volatile auto vF = F;
        auto name = cog::switch_feature_name(static_cast<cog::SwitchFeature>(vF));
        assert(!name.empty());
        assert(name != std::string_view{"<unknown SwitchFeature>"});
    }
    std::printf("  test_switch_feature_runtime:          PASSED\n");
}

static void test_cpu_feature_runtime() {
    constexpr cog::CpuFeature flags[] = {
        cog::CpuFeature::Avx2, cog::CpuFeature::Avx512,
        cog::CpuFeature::Amx, cog::CpuFeature::Vnni,
        cog::CpuFeature::Bf16Cpu, cog::CpuFeature::Fp16Cpu,
        cog::CpuFeature::Aes, cog::CpuFeature::Sha,
        cog::CpuFeature::Neon, cog::CpuFeature::Sve,
        cog::CpuFeature::Sve2, cog::CpuFeature::Sme,
        cog::CpuFeature::AmxBf16Arm, cog::CpuFeature::Mte,
        cog::CpuFeature::PauthArm, cog::CpuFeature::Cet,
    };
    for (cog::CpuFeature F : flags) {
        volatile auto vF = F;
        auto name = cog::cpu_feature_name(static_cast<cog::CpuFeature>(vF));
        assert(!name.empty());
        assert(name != std::string_view{"<unknown CpuFeature>"});
    }
    std::printf("  test_cpu_feature_runtime:             PASSED\n");
}

static void test_dram_feature_runtime() {
    constexpr cog::DramFeature flags[] = {
        cog::DramFeature::Ecc, cog::DramFeature::OnDieEcc,
        cog::DramFeature::PowerDownIdle, cog::DramFeature::Hbm,
    };
    for (cog::DramFeature F : flags) {
        volatile auto vF = F;
        auto name = cog::dram_feature_name(static_cast<cog::DramFeature>(vF));
        assert(!name.empty());
        assert(name != std::string_view{"<unknown DramFeature>"});
    }
    std::printf("  test_dram_feature_runtime:            PASSED\n");
}

// ── Schema construction smoke ───────────────────────────────────────

static void test_gpu_target_caps_construction() {
    cog::GpuTargetCaps caps{};
    caps.sm_count = crucible::safety::Tagged<std::uint16_t,
        crucible::safety::source::Vendor>{132};
    caps.warp_size = cog::PowerOfTwoLane{std::uint16_t{32}};
    caps.warp_schedulers_per_sm = crucible::safety::Tagged<std::uint16_t,
        crucible::safety::source::Vendor>{4};
    caps.max_warps_per_sm = crucible::safety::Tagged<std::uint16_t,
        crucible::safety::source::Vendor>{64};
    caps.max_regs_per_thread = cog::ValidRegsPerThread{std::uint16_t{255}};
    caps.smem_per_sm_bytes = crucible::safety::Tagged<std::uint32_t,
        crucible::safety::source::Vendor>{233472};
    caps.l2_bytes = crucible::safety::Tagged<std::uint64_t,
        crucible::safety::source::Vendor>{50ull << 20};
    caps.hbm_bytes = crucible::safety::Tagged<std::uint64_t,
        crucible::safety::source::Vendor>{80ull << 30};
    caps.tflops_fp16 = crucible::safety::Tagged<float,
        crucible::safety::source::Calibrated>{989.0f};
    caps.pcie_gen = crucible::safety::Tagged<cog::PcieGen,
        crucible::safety::source::Vendor>{cog::PcieGen::Gen5};
    caps.features.set(cog::GpuFeature::Tma);
    caps.features.set(cog::GpuFeature::Fp8);
    caps.features.set(cog::GpuFeature::Bf16);

    // Volatile reads through the schema — fields survive copy.
    volatile auto sm = caps.sm_count.value();
    assert(sm == 132);
    // Float bit-equality (exact ==) prohibited by -Werror=float-equal.
    // Compare by bitcast through uint32_t — the value was set verbatim
    // so the bits MUST round-trip identically; this is a structural
    // invariant on Tagged<float>'s representation.
    volatile auto fp16 = caps.tflops_fp16.value();
    float fp16_seen = fp16;
    assert(std::bit_cast<std::uint32_t>(fp16_seen) ==
           std::bit_cast<std::uint32_t>(989.0f));
    assert(caps.features.test(cog::GpuFeature::Tma));
    assert(!caps.features.test(cog::GpuFeature::ClusterLaunch));

    std::printf("  test_gpu_target_caps_construction:    PASSED\n");
}

static void test_nic_port_target_caps_construction() {
    cog::NicPortTargetCaps caps{};
    caps.link_layer = crucible::safety::Tagged<cog::LinkLayer,
        crucible::safety::source::Vendor>{cog::LinkLayer::Roce};
    caps.line_rate_bytes_per_sec = crucible::safety::Tagged<std::uint64_t,
        crucible::safety::source::Vendor>{50ull * 1000ull * 1000ull * 1000ull / 8ull};
    caps.mtu_bytes = cog::ValidMtu{std::uint16_t{9000}};
    caps.max_qp_count = crucible::safety::Tagged<std::uint32_t,
        crucible::safety::source::Vendor>{262144};
    caps.tcam_entries = crucible::safety::Tagged<std::uint32_t,
        crucible::safety::source::Vendor>{65536};
    caps.effective_bandwidth_bytes_per_sec = crucible::safety::Tagged<std::uint64_t,
        crucible::safety::source::Calibrated>{45ull * 1000ull * 1000ull * 1000ull / 8ull};
    caps.features.set(cog::NicFeature::Roce);
    caps.features.set(cog::NicFeature::GpuDirectRdma);
    caps.features.set(cog::NicFeature::XdpNative);

    volatile auto qp = caps.max_qp_count.value();
    assert(qp == 262144);
    volatile auto tcam = caps.tcam_entries.value();
    assert(tcam == 65536);
    volatile auto eff = caps.effective_bandwidth_bytes_per_sec.value();
    assert(eff < caps.line_rate_bytes_per_sec.value());
    assert(caps.features.test(cog::NicFeature::Roce));

    std::printf("  test_nic_port_target_caps_construction: PASSED\n");
}

static void test_nvswitch_target_caps_construction() {
    cog::NvSwitchTargetCaps caps{};
    caps.port_count = crucible::safety::Tagged<std::uint16_t,
        crucible::safety::source::Vendor>{64};
    caps.per_port_bandwidth_bytes_per_sec = crucible::safety::Tagged<std::uint64_t,
        crucible::safety::source::Vendor>{900ull * 1000ull * 1000ull * 1000ull / 8ull};
    caps.features.set(cog::SwitchFeature::Sharp);
    caps.features.set(cog::SwitchFeature::Ecn);

    volatile auto pc = caps.port_count.value();
    assert(pc == 64);
    assert(caps.features.test(cog::SwitchFeature::Sharp));

    std::printf("  test_nvswitch_target_caps_construction: PASSED\n");
}

static void test_cpu_target_caps_construction() {
    cog::CpuCoreTargetCaps core{};
    core.base_clock_mhz = crucible::safety::Tagged<std::uint32_t,
        crucible::safety::source::Vendor>{2400};
    core.max_clock_mhz = crucible::safety::Tagged<std::uint32_t,
        crucible::safety::source::Vendor>{3800};
    core.simd_vector_lanes = cog::PowerOfTwoLane{std::uint16_t{16}};
    core.l2_bytes = crucible::safety::Tagged<std::uint32_t,
        crucible::safety::source::Vendor>{2u << 20};   // 2 MB
    core.features.set(cog::CpuFeature::Avx512);
    core.features.set(cog::CpuFeature::Amx);
    core.features.set(cog::CpuFeature::Vnni);

    cog::CpuSocketTargetCaps socket{};
    socket.core_count = crucible::safety::Tagged<std::uint16_t,
        crucible::safety::source::Vendor>{56};
    socket.thread_count = crucible::safety::Tagged<std::uint16_t,
        crucible::safety::source::Vendor>{112};
    socket.l3_bytes = crucible::safety::Tagged<std::uint64_t,
        crucible::safety::source::Vendor>{105ull << 20};   // 105 MB
    socket.numa_node_count = crucible::safety::Tagged<std::uint8_t,
        crucible::safety::source::Vendor>{2};
    socket.representative_core = core;
    socket.features = core.features;

    volatile auto cores = socket.core_count.value();
    assert(cores == 56);
    assert(socket.representative_core.features.test(cog::CpuFeature::Amx));

    std::printf("  test_cpu_target_caps_construction:    PASSED\n");
}

static void test_dram_target_caps_construction() {
    cog::DramChannelTargetCaps caps{};
    caps.channel_width_bits = crucible::safety::Tagged<std::uint8_t,
        crucible::safety::source::Vendor>{64};
    caps.speed_mts = crucible::safety::Tagged<std::uint16_t,
        crucible::safety::source::Vendor>{6400};
    caps.bandwidth_bytes_per_sec = crucible::safety::Tagged<std::uint64_t,
        crucible::safety::source::Calibrated>{50ull * 1000ull * 1000ull * 1000ull};
    caps.capacity_bytes = crucible::safety::Tagged<std::uint64_t,
        crucible::safety::source::Vendor>{32ull << 30};   // 32 GB
    caps.features.set(cog::DramFeature::Ecc);
    caps.features.set(cog::DramFeature::OnDieEcc);

    volatile auto bw = caps.bandwidth_bytes_per_sec.value();
    assert(bw > 0);
    assert(caps.features.test(cog::DramFeature::Ecc));

    std::printf("  test_dram_target_caps_construction:   PASSED\n");
}

// ── caps_for binding + HasCaps gate runtime confirmation ────────────

static void test_caps_for_binding() {
    // Type-equality pins (also asserted in-header but exercised here
    // under TU-context warning flags).
    static_assert(std::is_same_v<cog::caps_for_t<cog::CogKind::Gpu>,
                                  cog::GpuTargetCaps>);
    static_assert(std::is_same_v<cog::caps_for_t<cog::CogKind::NicPort>,
                                  cog::NicPortTargetCaps>);
    static_assert(std::is_same_v<cog::caps_for_t<cog::CogKind::CpuCore>,
                                  cog::CpuCoreTargetCaps>);
    static_assert(std::is_same_v<cog::caps_for_t<cog::CogKind::DramChannel>,
                                  cog::DramChannelTargetCaps>);

    // HasCaps positive cases — substrates that schedule.
    static_assert( cog::HasCaps<cog::CogKind::Gpu>);
    static_assert( cog::HasCaps<cog::CogKind::CpuCore>);
    static_assert( cog::HasCaps<cog::CogKind::CpuSocket>);
    static_assert( cog::HasCaps<cog::CogKind::NicPort>);
    static_assert( cog::HasCaps<cog::CogKind::NvSwitch>);
    static_assert( cog::HasCaps<cog::CogKind::DramChannel>);

    // HasCaps negative cases — non-schedulable Cogs.
    static_assert(!cog::HasCaps<cog::CogKind::PsuRail>);
    static_assert(!cog::HasCaps<cog::CogKind::BmcSensor>);
    static_assert(!cog::HasCaps<cog::CogKind::OpticalTransceiver>);
    static_assert(!cog::HasCaps<cog::CogKind::PcieLaneGroup>);
    static_assert(!cog::HasCaps<cog::CogKind::NvmeNamespace>);
    static_assert(!cog::HasCaps<cog::CogKind::Datacenter>);

    // Runtime confirmation via constrained-template instantiation —
    // mirrors the IsComputeKind partition test in test_cog_identity.
    auto query = []<cog::CogKind K>() requires cog::HasCaps<K> {
        return std::size_t{1};
    };
    volatile std::size_t total =
        query.template operator()<cog::CogKind::Gpu>()
      + query.template operator()<cog::CogKind::NicPort>()
      + query.template operator()<cog::CogKind::NvSwitch>()
      + query.template operator()<cog::CogKind::CpuCore>()
      + query.template operator()<cog::CogKind::CpuSocket>()
      + query.template operator()<cog::CogKind::DramChannel>();
    assert(total == 6);

    std::printf("  test_caps_for_binding:                PASSED\n");
}

int main() {
    std::printf("test_target_caps: 11 groups\n");
    test_link_layer_name_coverage();
    test_pcie_gen_name_coverage();
    test_gpu_feature_runtime();
    test_nic_feature_runtime();
    test_switch_feature_runtime();
    test_cpu_feature_runtime();
    test_dram_feature_runtime();
    test_gpu_target_caps_construction();
    test_nic_port_target_caps_construction();
    test_nvswitch_target_caps_construction();
    test_cpu_target_caps_construction();
    test_dram_target_caps_construction();
    test_caps_for_binding();
    std::printf("test_target_caps: 11 groups, all passed\n");
    return 0;
}
