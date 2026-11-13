#include <crucible/cog/Calibrate.h>
#include <fixy/Ctx.h>
#include <foundation/effects/Ctx.h>
#include <foundation/reflect/EnumName.h>

#include <array>
#include "test_assert.h"
#include <cstdio>
#include <limits>
#include <span>
#include <string_view>
#include <type_traits>

namespace cog = crucible::cog;
namespace eff = ::fixy;
namespace testing = ::foundation::effects::testing;

namespace {

cog::CogIdentity gpu_identity() {
    cog::CogIdentity id{};
    id.uuid = cog::Uuid{0x196, 0x600};
    id.kind = cog::CogKind::Gpu;
    return id;
}

cog::CogIdentity nic_identity() {
    cog::CogIdentity id{};
    id.uuid = cog::Uuid{0x196, 0xC0A6};
    id.kind = cog::CogKind::NicPort;
    return id;
}

void test_admission() {
    auto iterations = cog::admit_calibration_iterations(1000);
    assert(iterations.is_ok());
    assert(std::move(iterations).expect("1000 iterations fit").value() == 1000u);

    auto bad_iterations = cog::admit_calibration_iterations(0);
    assert(bad_iterations.is_err());
    assert(bad_iterations.err() == ::fixy::some(cog::CalibrationError::InvalidIterations));

    auto warmup = cog::admit_warmup_iterations(0);
    assert(warmup.is_ok());
    assert(std::move(warmup).expect("no warmup fits").value() == 0u);

    auto trim = cog::admit_trim_basis_points(100);
    assert(trim.is_ok());
    assert(std::move(trim).expect("100 basis points fit").value() == 100u);

    auto too_much_trim = cog::admit_trim_basis_points(1001);
    assert(too_much_trim.is_err());
    assert(too_much_trim.err() == ::fixy::some(cog::CalibrationError::InvalidTrimBasisPoints));

    auto latency = cog::admit_latency_quantiles(cog::LatencyQuantiles{100u, 500u, 900u});
    assert(latency.is_ok());
    assert(std::move(latency).expect("the quantiles are ordered").value().p99_ns == 500u);

    auto zero_latency = cog::admit_latency_quantiles(cog::LatencyQuantiles{0u, 1u, 2u});
    assert(zero_latency.is_err());
    assert(zero_latency.err() == ::fixy::some(cog::CalibrationError::InvalidLatencyQuantiles));

    auto inverted_latency = cog::admit_latency_quantiles(cog::LatencyQuantiles{20u, 10u, 30u});
    assert(inverted_latency.is_err());
    assert(inverted_latency.err() == ::fixy::some(cog::CalibrationError::InvalidLatencyQuantiles));

    auto throughput = cog::admit_throughput_per_sec(989.0e12);
    assert(throughput.is_ok());
    assert(std::move(throughput).expect("the throughput is finite and positive").value() > 900.0e12);

    auto bad_throughput = cog::admit_throughput_per_sec(-1.0);
    assert(bad_throughput.is_err());
    assert(bad_throughput.err() == ::fixy::some(cog::CalibrationError::InvalidThroughput));

    // A NaN fails the positive conjunct and an infinity fails the ceiling.
    assert(cog::admit_throughput_per_sec(std::numeric_limits<double>::quiet_NaN()).is_err());
    assert(cog::admit_throughput_per_sec(std::numeric_limits<double>::infinity()).is_err());
    assert(cog::admit_throughput_per_sec(0.0).is_err());

    auto bad_drift = cog::admit_drift_basis_points(0);
    assert(bad_drift.is_err());
    assert(bad_drift.err() == ::fixy::some(cog::CalibrationError::InvalidDriftBasisPoints));
    assert(cog::admit_drift_basis_points(10001).is_err());
    assert(cog::admit_drift_basis_points(10000).is_ok());

    assert(cog::admit_runtime_budget_ms(0).is_err());
    assert(cog::admit_runtime_budget_ms(86400000u).is_ok());
    assert(cog::admit_runtime_budget_ms(86400001u).is_err());

    // A count above the storage width is refused before the narrowing,
    // never truncated into range.
    auto wide_count = cog::admit_sample_count(65536u + 7u);
    assert(wide_count.is_err());
    assert(wide_count.err() == ::fixy::some(cog::CalibrationError::InvalidSampleCount));
    assert(cog::admit_sample_count(0u).is_err());
    assert(cog::admit_sample_count(65535u).expect("65535 samples fit").value() == 65535u);

    crucible::test::pass("  test_admission: PASSED\n");
}

void test_latency_entry_and_result() {
    auto latency = cog::admit_latency_quantiles(cog::LatencyQuantiles{100u, 500u, 900u});
    auto throughput = cog::admit_throughput_per_sec(989.0e12);
    auto samples = cog::admit_sample_count(1024);
    assert(latency.is_ok());
    assert(throughput.is_ok());
    assert(samples.is_ok());

    cog::CalibrationSample<cog::CogKind::Gpu> sample{
        .opcode = cog::GpuOpcode::GemmPlain,
        .size_bucket = cog::SizeBucket::S4096,
        .dtype_bucket = cog::DtypeBucket::Bf16,
        .transpose_mode = cog::TransposeMode::Nn,
        .message_size_bucket = cog::MessageSizeBucket::None,
        .latency_cycles = 320000u,
        .latency = std::move(latency).expect("the quantiles are ordered"),
        .throughput_per_sec = std::move(throughput).expect("the throughput is finite and positive"),
        .sample_count = std::move(samples).expect("1024 samples fit"),
    };
    auto entry = cog::make_latency_entry(sample);
    assert(entry.opcode == cog::GpuOpcode::GemmPlain);
    assert(entry.latency.value().p50_ns == 100u);
    assert(entry.sample_count.value() == 1024u);

    auto valid_entry = cog::validate_latency_entry(entry);
    assert(valid_entry.is_ok());

    cog::GpuTargetCaps caps{};
    caps.tflops_fp16 = ::fixy::mint_tagged<::fixy::tags::source::Calibrated, float>(989.0f);
    caps.features.set(cog::GpuFeature::Fp8);

    ::foundation::core::FixedArray<cog::OpcodeLatencyEntry<cog::CogKind::Gpu>, 1> rows{entry};
    auto result = cog::build_calibration_result<cog::CogKind::Gpu>(gpu_identity(), caps, rows.view())
                      .expect("a Gpu identity and one valid row make a result");
    assert(result.identity.kind == cog::CogKind::Gpu);
    assert(result.entry_count.value() == 1u);
    assert(result.opcode_table.size() == 1u);
    assert(result.opcode_table.lookup_by_opcode(cog::GpuOpcode::GemmPlain).is_some());
    assert(result.opcode_table.calibration_age_seconds.is_fresh());

    auto mismatched = cog::build_calibration_result<cog::CogKind::Gpu>(nic_identity(), caps, rows.view());
    assert(mismatched.err() == ::fixy::some(cog::CalibrationError::KindMismatch));

    crucible::test::pass("  test_latency_entry_and_result: PASSED\n");
}

void test_nic_result() {
    auto latency = cog::admit_latency_quantiles(cog::LatencyQuantiles{1500u, 2500u, 5500u});
    auto throughput = cog::admit_throughput_per_sec(6.25e9);
    auto samples = cog::admit_sample_count(4096);
    assert(latency.is_ok());
    assert(throughput.is_ok());
    assert(samples.is_ok());

    cog::CalibrationSample<cog::CogKind::NicPort> sample{
        .opcode = cog::NicOpcode::RdmaWrite,
        .message_size_bucket = cog::MessageSizeBucket::M64B,
        .latency_cycles = 4500u,
        .latency = std::move(latency).expect("the quantiles are ordered"),
        .throughput_per_sec = std::move(throughput).expect("the throughput is finite and positive"),
        .sample_count = std::move(samples).expect("4096 samples fit"),
    };
    auto entry = cog::make_latency_entry(sample);
    ::foundation::core::FixedArray<cog::OpcodeLatencyEntry<cog::CogKind::NicPort>, 1> rows{entry};

    cog::NicPortTargetCaps caps{};
    caps.effective_bandwidth_bytes_per_sec =
        ::fixy::mint_tagged<::fixy::tags::source::Calibrated, std::uint64_t>(6250000000ull);
    caps.features.set(cog::NicFeature::Roce);

    auto result = cog::build_calibration_result<cog::CogKind::NicPort>(nic_identity(), caps, rows.view())
                      .expect("a NicPort identity and one valid row make a result");
    auto found = result.opcode_table
                     .latency_for_size_bucket(cog::NicOpcode::RdmaWrite, cog::SizeBucket::None, cog::DtypeBucket::None,
                                              cog::TransposeMode::Nn, cog::MessageSizeBucket::M64B)
                     .expect("the table holds the row of the 64-byte bucket");
    assert(found.latency.value().p99_ns == 2500u);

    crucible::test::pass("  test_nic_result: PASSED\n");
}

eff::ColdInitCtx init_ctx() { return eff::ColdInitCtx{testing::init()}; }
eff::BgDrainCtx bg_ctx() { return eff::BgDrainCtx{testing::bg()}; }

void test_backend_boundaries() {
    auto init = cog::calibrate_cog<cog::CogKind::Gpu>(init_ctx(), gpu_identity());
    assert(init.err() == ::fixy::some(cog::CalibrationError::BackendUnavailable));

    auto bg = cog::calibrate_cog<cog::CogKind::Gpu>(bg_ctx(), gpu_identity());
    assert(bg.err() == ::fixy::some(cog::CalibrationError::BackendUnavailable));

    auto wrong_kind = cog::calibrate_cog<cog::CogKind::Gpu>(init_ctx(), nic_identity());
    assert(wrong_kind.err() == ::fixy::some(cog::CalibrationError::KindMismatch));

    ::foundation::core::FixedArray<cog::GpuOpcode, 1> opcodes{cog::GpuOpcode::GemmPlain};
    auto specific = cog::calibrate_specific_opcodes<cog::CogKind::Gpu>(init_ctx(), gpu_identity(), opcodes.view());
    assert(specific.err() == ::fixy::some(cog::CalibrationError::BackendUnavailable));

    auto empty_specific = cog::calibrate_specific_opcodes<cog::CogKind::Gpu>(init_ctx(), gpu_identity(),
                                                                             ::fixy::View<cog::GpuOpcode const>{});
    assert(empty_specific.err() == ::fixy::some(cog::CalibrationError::EmptyOpcodeSet));

    crucible::test::pass("  test_backend_boundaries: PASSED\n");
}

void test_drift_gate() {
    cog::DriftSignal below{
        .observed_drift_bps = cog::admit_drift_basis_points(500).expect("500 basis points fit"),
        .threshold_bps = cog::admit_drift_basis_points(1000).expect("1000 basis points fit"),
    };
    assert(!cog::should_recalibrate(below));
    auto below_result = cog::recalibrate_drifted<cog::CogKind::Gpu>(bg_ctx(), gpu_identity(), below);
    assert(below_result.err() == ::fixy::some(cog::CalibrationError::DriftBelowThreshold));

    cog::DriftSignal above{
        .observed_drift_bps = cog::admit_drift_basis_points(1200).expect("1200 basis points fit"),
        .threshold_bps = cog::admit_drift_basis_points(1000).expect("1000 basis points fit"),
    };
    assert(cog::should_recalibrate(above));
    auto above_result = cog::recalibrate_drifted<cog::CogKind::Gpu>(bg_ctx(), gpu_identity(), above);
    assert(above_result.err() == ::fixy::some(cog::CalibrationError::BackendUnavailable));

    crucible::test::pass("  test_drift_gate: PASSED\n");
}

}  // namespace

int main() {
    static_assert(sizeof(cog::CalibrationLatencyQuantiles) == sizeof(cog::LatencyQuantiles));
    static_assert(sizeof(cog::CalibratedThroughput) == sizeof(double));
    static_assert(cog::CalibratableCogKind<cog::CogKind::Gpu>);
    static_assert(cog::CalibratableCogKind<cog::CogKind::NicPort>);
    static_assert(!cog::CalibratableCogKind<cog::CogKind::PsuRail>);
    static_assert(cog::CtxFitsCalibration<eff::ColdInitCtx>);
    static_assert(cog::CtxFitsCalibration<eff::BgDrainCtx>);
    static_assert(!cog::CtxFitsCalibration<eff::HotFgCtx>);
    static_assert(!cog::CtxFitsCalibration<eff::TestRunnerCtx>);
    static_assert(std::is_trivially_copy_constructible_v<cog::CalibrationPlan>);
    static_assert(!std::is_trivially_copyable_v<cog::CalibrationPlan>,
                  "a plan holds refined members, so no byte copy builds one");

    // The conjunction refuses both ways a triple can be wrong.
    static_assert(cog::calibration_quantiles_valid(cog::LatencyQuantiles{1u, 2u, 3u}));
    static_assert(!cog::calibration_quantiles_valid(cog::LatencyQuantiles{0u, 2u, 3u}));
    static_assert(!cog::calibration_quantiles_valid(cog::LatencyQuantiles{3u, 2u, 1u}));

    static_assert(::foundation::reflect::enum_name(cog::CalibrationError::BackendUnavailable) == "BackendUnavailable");
    static_assert(::foundation::reflect::enum_name(cog::CalibrationError::InvalidDriftBasisPoints)
                  == "InvalidDriftBasisPoints");
    static_assert(::foundation::reflect::enum_name(static_cast<cog::CalibrationError>(200))
                  == "<unknown CalibrationError>");

    ::fixy::report(::fixy::Sink::Out, "test_calibrate:\n");
    test_admission();
    test_latency_entry_and_result();
    test_nic_result();
    test_backend_boundaries();
    test_drift_gate();
    crucible::test::pass("test_calibrate: all PASSED\n");
    return 0;
}
