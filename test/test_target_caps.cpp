// Including the header is itself part of the claim.  The frozen-position
// pins and layout pins it carries are never compiled under the project
// warning flags until some translation unit pulls it in.
//
// Every enumerator name below is read with a non-constant argument, so a
// broken lookup shows up under runtime evaluation and not only when the
// compiler folds the call.

#include <crucible/cog/TargetCaps.h>
#include <foundation/reflect/EnumName.h>

#include "padding_bytes.h"
#include "test_assert.h"

#include <bit>
#include <cstdint>
#include <cstdio>
#include <string_view>
#include <type_traits>

namespace cog = crucible::cog;
namespace fr = ::foundation::reflect;

// Name the stored type, so a literal of another width does not deduce a
// claim the field refuses.
template <typename T>
static constexpr cog::VendorClaim<T> vendor(std::type_identity_t<T> value) noexcept {
    return ::fixy::mint_tagged<::fixy::tags::source::Vendor, T>(value);
}

template <typename T>
static constexpr cog::CalibratedValue<T> calibrated(std::type_identity_t<T> value) noexcept {
    return ::fixy::mint_tagged<::fixy::tags::source::Calibrated, T>(value);
}

// Each enumerator's name comes back through a volatile, so the lookup runs
// at run time as well as in the constant folder.
template <typename E>
static void check_every_name_at_run_time() {
    fr::for_each_enumerator<E>([](E value, std::string_view identifier) {
        volatile E runtime_value = value;
        const std::string_view name = fr::enum_name(static_cast<E>(runtime_value));
        assert(name == identifier);
        assert(name != fr::unknown_enum_sentinel<E>);
    });
}

// A deleted feature leaves its bit empty, because a stored schema can still
// hold that bit.  No enumerator names the bits below.
static_assert(fr::enum_name(static_cast<cog::NicFeature>(1u << 7)) == fr::unknown_enum_sentinel<cog::NicFeature>);
static_assert(fr::enum_name(static_cast<cog::SwitchFeature>(1u << 1)) == fr::unknown_enum_sentinel<cog::SwitchFeature>);
static_assert(fr::enum_name(static_cast<cog::SwitchFeature>(1u << 7)) == fr::unknown_enum_sentinel<cog::SwitchFeature>);

static void test_names_at_run_time() {
    check_every_name_at_run_time<cog::LinkLayer>();
    check_every_name_at_run_time<cog::PcieGen>();
    check_every_name_at_run_time<cog::GpuFeature>();
    check_every_name_at_run_time<cog::NicFeature>();
    check_every_name_at_run_time<cog::SwitchFeature>();
    check_every_name_at_run_time<cog::CpuFeature>();
    check_every_name_at_run_time<cog::DramFeature>();
    crucible::test::pass("  test_names_at_run_time:               PASSED\n");
}

// The underlying value of each PCIe generation is the generation number,
// so GenN holds N.
static void test_pcie_gen_value_is_generation() {
    fr::for_each_enumerator<cog::PcieGen>([](cog::PcieGen gen, std::string_view identifier) {
        volatile auto runtime_value = static_cast<std::uint8_t>(gen);
        const std::uint8_t value = runtime_value;
        if (identifier == "None") {
            assert(value == 0);
        } else {
            assert(identifier.size() == 4 && identifier.starts_with("Gen"));
            assert(value == static_cast<std::uint8_t>(identifier[3] - '0'));
        }
    });
    crucible::test::pass("  test_pcie_gen_value_is_generation:    PASSED\n");
}

static void test_gpu_feature_runtime() {
    ::fixy::Bits<cog::GpuFeature> all_set{};
    fr::for_each_enumerator<cog::GpuFeature>([&](cog::GpuFeature flag, std::string_view) { all_set.set(flag); });
    assert(all_set.popcount() == static_cast<int>(fr::enum_count<cog::GpuFeature>));
    assert(all_set.test(cog::GpuFeature::Tma));
    assert(all_set.test(cog::GpuFeature::Fp8));

    // A bitset over a second enum appears here to show the two
    // instantiations keep separate storage.  The type system already
    // forbids confusing them; this only confirms the counts.
    ::fixy::Bits<cog::NicFeature> nic_bits{};
    nic_bits.set(cog::NicFeature::Tso);
    assert(nic_bits.popcount() == 1);
    assert(!nic_bits.test(cog::NicFeature::Roce));

    crucible::test::pass("  test_gpu_feature_runtime:             PASSED\n");
}

static void test_gpu_target_caps_construction() {
    cog::GpuTargetCaps caps{};
    caps.sm_count = vendor<std::uint16_t>(132);
    caps.warp_size = ::fixy::mint_refined<cog::power_of_two_lane>(std::uint16_t{32});
    caps.warp_schedulers_per_sm = vendor<std::uint16_t>(4);
    caps.max_warps_per_sm = vendor<std::uint16_t>(64);
    caps.max_regs_per_thread = ::fixy::mint_refined<cog::valid_regs_per_thread>(std::uint16_t{255});
    caps.smem_per_sm_bytes = vendor<std::uint32_t>(233472);
    caps.l2_bytes = vendor<std::uint64_t>(50ull << 20);
    caps.hbm_bytes = vendor<std::uint64_t>(80ull << 30);
    caps.tflops_fp16 = calibrated<float>(989.0f);
    caps.pcie_gen = vendor<cog::PcieGen>(cog::PcieGen::Gen5);
    caps.features.set(cog::GpuFeature::Tma);
    caps.features.set(cog::GpuFeature::Fp8);
    caps.features.set(cog::GpuFeature::Bf16);

    volatile auto sm = caps.sm_count.value();
    assert(sm == 132);
    // Float equality is a hard error under the project warning flags, so
    // the comparison goes through the bit pattern.  The value was stored
    // verbatim, so the bits have to come back unchanged.
    volatile auto fp16 = caps.tflops_fp16.value();
    float fp16_seen = fp16;
    assert(std::bit_cast<std::uint32_t>(fp16_seen) == std::bit_cast<std::uint32_t>(989.0f));
    assert(caps.features.test(cog::GpuFeature::Tma));
    assert(!caps.features.test(cog::GpuFeature::ClusterLaunch));

    crucible::test::pass("  test_gpu_target_caps_construction:    PASSED\n");
}

static void test_nic_port_target_caps_construction() {
    cog::NicPortTargetCaps caps{};
    caps.link_layer = vendor<cog::LinkLayer>(cog::LinkLayer::Roce);
    caps.line_rate_bytes_per_sec = vendor<std::uint64_t>(50ull * 1000ull * 1000ull * 1000ull / 8ull);
    caps.mtu_bytes = ::fixy::mint_refined<cog::valid_mtu>(std::uint16_t{9000});
    caps.max_qp_count = vendor<std::uint32_t>(262144);
    caps.tcam_entries = vendor<std::uint32_t>(65536);
    caps.effective_bandwidth_bytes_per_sec = calibrated<std::uint64_t>(45ull * 1000ull * 1000ull * 1000ull / 8ull);
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

    crucible::test::pass("  test_nic_port_target_caps_construction: PASSED\n");
}

static void test_nvswitch_target_caps_construction() {
    cog::NvSwitchTargetCaps caps{};
    caps.port_count = vendor<std::uint16_t>(64);
    caps.per_port_bandwidth_bytes_per_sec = vendor<std::uint64_t>(900ull * 1000ull * 1000ull * 1000ull / 8ull);
    caps.features.set(cog::SwitchFeature::Sharp);
    caps.features.set(cog::SwitchFeature::Ecn);

    volatile auto pc = caps.port_count.value();
    assert(pc == 64);
    assert(caps.features.test(cog::SwitchFeature::Sharp));

    crucible::test::pass("  test_nvswitch_target_caps_construction: PASSED\n");
}

static void test_cpu_target_caps_construction() {
    cog::CpuCoreTargetCaps core{};
    core.base_clock_mhz = vendor<std::uint32_t>(2400);
    core.max_clock_mhz = vendor<std::uint32_t>(3800);
    core.simd_vector_lanes = ::fixy::mint_refined<cog::power_of_two_lane>(std::uint16_t{16});
    core.l2_bytes = vendor<std::uint32_t>(2u << 20);  // 2 MB
    core.features.set(cog::CpuFeature::Avx512);
    core.features.set(cog::CpuFeature::Amx);
    core.features.set(cog::CpuFeature::Vnni);

    cog::CpuSocketTargetCaps socket{};
    socket.core_count = vendor<std::uint16_t>(56);
    socket.thread_count = vendor<std::uint16_t>(112);
    socket.l3_bytes = vendor<std::uint64_t>(105ull << 20);  // 105 MB
    socket.numa_node_count = vendor<std::uint8_t>(2);
    socket.representative_core = core;
    socket.features = core.features;

    volatile auto cores = socket.core_count.value();
    assert(cores == 56);
    assert(socket.representative_core.features.test(cog::CpuFeature::Amx));

    crucible::test::pass("  test_cpu_target_caps_construction:    PASSED\n");
}

static void test_dram_target_caps_construction() {
    cog::DramChannelTargetCaps caps{};
    caps.channel_width_bits = vendor<std::uint8_t>(64);
    caps.speed_mts = vendor<std::uint16_t>(6400);
    caps.bandwidth_bytes_per_sec = calibrated<std::uint64_t>(50ull * 1000ull * 1000ull * 1000ull);
    caps.capacity_bytes = vendor<std::uint64_t>(32ull << 30);  // 32 GB
    caps.features.set(cog::DramFeature::Ecc);
    caps.features.set(cog::DramFeature::OnDieEcc);

    volatile auto bw = caps.bandwidth_bytes_per_sec.value();
    assert(bw > 0);
    assert(caps.features.test(cog::DramFeature::Ecc));

    crucible::test::pass("  test_dram_target_caps_construction:   PASSED\n");
}

static void test_caps_for_binding() {
    static_assert(std::is_same_v<cog::caps_for_t<cog::CogKind::Gpu>, cog::GpuTargetCaps>);
    static_assert(std::is_same_v<cog::caps_for_t<cog::CogKind::NicPort>, cog::NicPortTargetCaps>);
    static_assert(std::is_same_v<cog::caps_for_t<cog::CogKind::CpuCore>, cog::CpuCoreTargetCaps>);
    static_assert(std::is_same_v<cog::caps_for_t<cog::CogKind::DramChannel>, cog::DramChannelTargetCaps>);

    // A kind carries capabilities when work can be scheduled onto it.
    static_assert(cog::HasCaps<cog::CogKind::Gpu>);
    static_assert(cog::HasCaps<cog::CogKind::CpuCore>);
    static_assert(cog::HasCaps<cog::CogKind::CpuSocket>);
    static_assert(cog::HasCaps<cog::CogKind::NicPort>);
    static_assert(cog::HasCaps<cog::CogKind::NvSwitch>);
    static_assert(cog::HasCaps<cog::CogKind::DramChannel>);

    // The kinds below are observed or powered, never scheduled onto.
    static_assert(!cog::HasCaps<cog::CogKind::PsuRail>);
    static_assert(!cog::HasCaps<cog::CogKind::BmcSensor>);
    static_assert(!cog::HasCaps<cog::CogKind::OpticalTransceiver>);
    static_assert(!cog::HasCaps<cog::CogKind::PcieLaneGroup>);
    static_assert(!cog::HasCaps<cog::CogKind::NvmeNamespace>);
    static_assert(!cog::HasCaps<cog::CogKind::Datacenter>);

    auto query = []<cog::CogKind K>()
        requires cog::HasCaps<K>
    { return std::size_t{1}; };
    volatile std::size_t total =
        query.template operator()<cog::CogKind::Gpu>() + query.template operator()<cog::CogKind::NicPort>()
        + query.template operator()<cog::CogKind::NvSwitch>() + query.template operator()<cog::CogKind::CpuCore>()
        + query.template operator()<cog::CogKind::CpuSocket>() + query.template operator()<cog::CogKind::DramChannel>();
    assert(total == 6);

    crucible::test::pass("  test_caps_for_binding:                PASSED\n");
}

// Each node fact of a discovery snapshot holds one NIC port schema, and each
// padding byte of the schema costs one store for each of the 64 facts at each
// initialization of an automatic snapshot (padding_bytes.h).
static void test_nic_port_target_caps_has_no_padding_byte() {
    crucible::test::expect_no_padding_byte<^^cog::NicPortTargetCaps>();
    crucible::test::pass("  test_nic_port_target_caps_has_no_padding_byte: PASSED\n");
}

int main() {
    ::fixy::report(::fixy::Sink::Out, "test_target_caps: 10 groups\n");
    test_names_at_run_time();
    test_pcie_gen_value_is_generation();
    test_gpu_feature_runtime();
    test_gpu_target_caps_construction();
    test_nic_port_target_caps_construction();
    test_nvswitch_target_caps_construction();
    test_cpu_target_caps_construction();
    test_dram_target_caps_construction();
    test_caps_for_binding();
    test_nic_port_target_caps_has_no_padding_byte();
    crucible::test::pass("test_target_caps: 10 groups, all passed\n");
    return 0;
}
