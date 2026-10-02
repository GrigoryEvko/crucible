// Including the header here puts its in-header static_asserts into the
// build graph.  Every accessor below is driven with volatile arguments
// so a bad lookup fails at runtime, not only at consteval time.

#include <crucible/cog/OpcodeLatencyTable.h>
#include <foundation/reflect/EnumName.h>

#include "test_assert.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <span>
#include <string_view>
#include <type_traits>

namespace cog = crucible::cog;
namespace reflect = ::foundation::reflect;

template <typename T>
static constexpr cog::CalibratedValue<T> calibrated(std::type_identity_t<T> value) noexcept {
    return ::fixy::mint_tagged<::fixy::tags::source::Calibrated, T>(value);
}

static constexpr cog::OrderedLatencyQuantiles ordered(std::uint32_t p50_ns, std::uint32_t p99_ns,
                                                      std::uint32_t p999_ns) noexcept {
    return ::fixy::mint_refined<cog::quantile_ordered>(cog::LatencyQuantiles{p50_ns, p99_ns, p999_ns});
}

// Every enumerator of E reads back its own name, and a value that no
// enumerator holds reads back the sentinel.  The walk comes from the
// enum itself, so a new atom joins the check without a new line here.
template <reflect::ScopedEnum E>
static void check_enum_names(std::underlying_type_t<E> unheld_value) {
    std::size_t walked = 0;
    reflect::for_each_enumerator<E>([&](E value, std::string_view declared) {
        volatile auto runtime_value = value;
        const std::string_view name = reflect::enum_name(static_cast<E>(runtime_value));
        assert(!name.empty());
        assert(name == declared);
        assert(name != reflect::unknown_enum_sentinel<E>);
        ++walked;
    });
    assert(walked == reflect::enum_count<E>);

    volatile auto runtime_unheld = unheld_value;
    assert(reflect::enum_name(static_cast<E>(runtime_unheld)) == reflect::unknown_enum_sentinel<E>);
}

static void test_enum_names() {
    check_enum_names<cog::SizeBucket>(3);
    check_enum_names<cog::DtypeBucket>(200);
    check_enum_names<cog::TransposeMode>(200);
    check_enum_names<cog::MessageSizeBucket>(5);
    check_enum_names<cog::GpuOpcode>(999);
    check_enum_names<cog::NicOpcode>(999);
    check_enum_names<cog::SwitchOpcode>(999);
    check_enum_names<cog::CpuOpcode>(999);
    check_enum_names<cog::DramOpcode>(999);

    static_assert(reflect::enum_name(cog::GpuOpcode::Conv2D) == "Conv2D");
    static_assert(reflect::enum_name(cog::MessageSizeBucket::M64B) == "M64B");
    static_assert(reflect::unknown_enum_sentinel<cog::SizeBucket> == "<unknown SizeBucket>");
    crucible::test::pass("  test_enum_names:                      PASSED\n");
}

static void test_latency_quantiles_ordered_construction() {
    const cog::OrderedLatencyQuantiles quantiles = ordered(100u, 500u, 2000u);
    volatile auto p50 = quantiles.value().p50_ns;
    volatile auto p99 = quantiles.value().p99_ns;
    volatile auto p999 = quantiles.value().p999_ns;
    assert(p50 == 100u);
    assert(p99 == 500u);
    assert(p999 == 2000u);

    // Equal quantiles are the boundary of the ordering invariant.
    const cog::OrderedLatencyQuantiles flat = ordered(42u, 42u, 42u);
    assert(flat.value().p50_ns == 42u);

    // The predicate itself, which the ledger also reads.
    static_assert(cog::quantile_ordered(cog::LatencyQuantiles{1u, 2u, 3u}));
    static_assert(!cog::quantile_ordered(cog::LatencyQuantiles{2u, 1u, 3u}));
    static_assert(!cog::quantile_ordered(cog::LatencyQuantiles{1u, 3u, 2u}));

    static_assert(sizeof(cog::OrderedLatencyQuantiles) == sizeof(cog::LatencyQuantiles));

    crucible::test::pass("  test_latency_quantiles_construction:  PASSED\n");
}

static void test_gpu_opcode_table_construction() {
    using Entry = cog::OpcodeLatencyEntry<cog::CogKind::Gpu>;

    // The table's span borrows these rows, so they must outlive it.
    static constexpr std::array<Entry, 3> rows = {
        Entry{
            .opcode = cog::GpuOpcode::GemmPlain,
            .size_bucket = cog::SizeBucket::S4096,
            .dtype_bucket = cog::DtypeBucket::Bf16,
            .transpose_mode = cog::TransposeMode::Nn,
            .message_size_bucket = cog::MessageSizeBucket::None,
            .latency_cycles = 8500000u,
            .latency = ordered(2500000u, 4500000u, 7200000u),
            .throughput_per_sec = 989.0e12,
            .sample_count = calibrated<std::uint16_t>(1024),
        },
        Entry{
            .opcode = cog::GpuOpcode::GemmPlain,
            .size_bucket = cog::SizeBucket::S4096,
            .dtype_bucket = cog::DtypeBucket::Fp16,
            .transpose_mode = cog::TransposeMode::Nn,
            .message_size_bucket = cog::MessageSizeBucket::None,
            .latency_cycles = 8500000u,
            .latency = ordered(2600000u, 4700000u, 7500000u),
            .throughput_per_sec = 989.0e12,
            .sample_count = calibrated<std::uint16_t>(1024),
        },
        Entry{
            .opcode = cog::GpuOpcode::AllReduceRing,
            .size_bucket = cog::SizeBucket::None,
            .dtype_bucket = cog::DtypeBucket::Fp32,
            .transpose_mode = cog::TransposeMode::Nn,
            .message_size_bucket = cog::MessageSizeBucket::M4M,
            .latency_cycles = 1200000u,
            .latency = ordered(350000u, 600000u, 1400000u),
            .throughput_per_sec = 4.5e10,
            .sample_count = calibrated<std::uint16_t>(256),
        },
    };

    cog::OpcodeLatencyTable<cog::CogKind::Gpu> table{
        .entries = calibrated<std::span<const Entry>>(std::span<const Entry>{rows}),
        .calibration_age_seconds = ::fixy::Stale<double>::at(12.5, 0),
    };

    volatile auto sz = table.size();
    assert(sz == 3);
    volatile bool empty = table.empty();
    assert(!empty);

    auto found = table.lookup_by_opcode(cog::GpuOpcode::AllReduceRing);
    assert(found.has_value());
    assert(found->message_size_bucket == cog::MessageSizeBucket::M4M);

    auto missing = table.lookup_by_opcode(cog::GpuOpcode::Conv2D);
    assert(!missing.has_value());

    auto fullkey =
        table.latency_for_size_bucket(cog::GpuOpcode::GemmPlain, cog::SizeBucket::S4096, cog::DtypeBucket::Fp16);
    assert(fullkey.has_value());
    assert(fullkey->dtype_bucket == cog::DtypeBucket::Fp16);

    // Throughput envelope sums both GEMM rows.
    volatile double envelope = table.throughput_envelope(cog::GpuOpcode::GemmPlain);
    assert(envelope > 1.5e15);
    assert(envelope < 2.5e15);

    cog::OpcodeLatencyTable<cog::CogKind::Gpu> empty_table{};
    assert(empty_table.empty());
    assert(empty_table.size() == 0);
    assert(!empty_table.lookup_by_opcode(cog::GpuOpcode::GemmPlain).has_value());

    // A never-calibrated table must report infinite staleness, not
    // zero.  A zero reading looks fresh to a drift detector, which
    // then never schedules recalibration.
    volatile bool default_is_infinite = empty_table.calibration_age_seconds.is_infinite();
    assert(default_is_infinite);
    volatile bool default_is_finite = empty_table.calibration_age_seconds.is_finite();
    assert(!default_is_finite);

    // sample_count == 0 marks an uncalibrated row.  Consumers trust
    // no other field of an entry until sample_count is positive.
    cog::OpcodeLatencyEntry<cog::CogKind::Gpu> default_entry{};
    volatile auto default_sample_count = default_entry.sample_count.value();
    assert(default_sample_count == 0);

    crucible::test::pass("  test_gpu_opcode_table_construction:   PASSED\n");
}

static void test_nic_opcode_table_construction() {
    using Entry = cog::OpcodeLatencyEntry<cog::CogKind::NicPort>;

    static constexpr std::array<Entry, 2> rows = {
        Entry{
            .opcode = cog::NicOpcode::RdmaWrite,
            .size_bucket = cog::SizeBucket::None,
            .dtype_bucket = cog::DtypeBucket::None,
            .transpose_mode = cog::TransposeMode::Nn,
            .message_size_bucket = cog::MessageSizeBucket::M64B,
            .latency_cycles = 4500u,
            .latency = ordered(1500u, 2500u, 5500u),
            .throughput_per_sec = 1.5e7,
            .sample_count = calibrated<std::uint16_t>(4096),
        },
        Entry{
            .opcode = cog::NicOpcode::RdmaWrite,
            .size_bucket = cog::SizeBucket::None,
            .dtype_bucket = cog::DtypeBucket::None,
            .transpose_mode = cog::TransposeMode::Nn,
            .message_size_bucket = cog::MessageSizeBucket::M4M,
            .latency_cycles = 850000u,
            .latency = ordered(250000u, 450000u, 920000u),
            .throughput_per_sec = 6.25e9,
            .sample_count = calibrated<std::uint16_t>(2048),
        },
    };

    cog::OpcodeLatencyTable<cog::CogKind::NicPort> table{
        .entries = calibrated<std::span<const Entry>>(std::span<const Entry>{rows}),
        .calibration_age_seconds = ::fixy::Stale<double>::fresh(0.0),
    };

    auto small = table.latency_for_size_bucket(cog::NicOpcode::RdmaWrite, cog::SizeBucket::None, cog::DtypeBucket::None,
                                               cog::TransposeMode::Nn, cog::MessageSizeBucket::M64B);
    assert(small.has_value());
    volatile auto small_p99 = small->latency.value().p99_ns;
    assert(small_p99 == 2500u);

    auto big = table.latency_for_size_bucket(cog::NicOpcode::RdmaWrite, cog::SizeBucket::None, cog::DtypeBucket::None,
                                             cog::TransposeMode::Nn, cog::MessageSizeBucket::M4M);
    assert(big.has_value());
    volatile auto big_p99 = big->latency.value().p99_ns;
    assert(big_p99 == 450000u);

    assert(table.calibration_age_seconds.is_fresh());

    crucible::test::pass("  test_nic_opcode_table_construction:   PASSED\n");
}

static void test_opcodes_for_binding() {
    static_assert(std::is_same_v<cog::opcodes_for_t<cog::CogKind::Gpu>, cog::GpuOpcode>);
    static_assert(std::is_same_v<cog::opcodes_for_t<cog::CogKind::CpuCore>, cog::CpuOpcode>);
    static_assert(std::is_same_v<cog::opcodes_for_t<cog::CogKind::CpuSocket>, cog::CpuOpcode>);
    static_assert(std::is_same_v<cog::opcodes_for_t<cog::CogKind::NicPort>, cog::NicOpcode>);
    static_assert(std::is_same_v<cog::opcodes_for_t<cog::CogKind::NvSwitch>, cog::SwitchOpcode>);
    static_assert(std::is_same_v<cog::opcodes_for_t<cog::CogKind::DramChannel>, cog::DramOpcode>);

    static_assert(cog::HasOpcodeTable<cog::CogKind::Gpu>);
    static_assert(cog::HasOpcodeTable<cog::CogKind::CpuCore>);
    static_assert(cog::HasOpcodeTable<cog::CogKind::CpuSocket>);
    static_assert(cog::HasOpcodeTable<cog::CogKind::NicPort>);
    static_assert(cog::HasOpcodeTable<cog::CogKind::NvSwitch>);
    static_assert(cog::HasOpcodeTable<cog::CogKind::DramChannel>);

    static_assert(!cog::HasOpcodeTable<cog::CogKind::PsuRail>);
    static_assert(!cog::HasOpcodeTable<cog::CogKind::BmcSensor>);
    static_assert(!cog::HasOpcodeTable<cog::CogKind::OpticalTransceiver>);
    static_assert(!cog::HasOpcodeTable<cog::CogKind::PcieLaneGroup>);
    static_assert(!cog::HasOpcodeTable<cog::CogKind::Datacenter>);

    auto query = []<cog::CogKind K>()
        requires cog::HasOpcodeTable<K>
    { return std::size_t{1}; };
    volatile std::size_t total =
        query.template operator()<cog::CogKind::Gpu>() + query.template operator()<cog::CogKind::NicPort>()
        + query.template operator()<cog::CogKind::NvSwitch>() + query.template operator()<cog::CogKind::CpuCore>()
        + query.template operator()<cog::CogKind::CpuSocket>() + query.template operator()<cog::CogKind::DramChannel>();
    assert(total == 6);

    crucible::test::pass("  test_opcodes_for_binding:             PASSED\n");
}

int main() {
    ::fixy::report(::fixy::Sink::Out, "test_opcode_latency_table: 5 groups\n");
    test_enum_names();
    test_latency_quantiles_ordered_construction();
    test_gpu_opcode_table_construction();
    test_nic_opcode_table_construction();
    test_opcodes_for_binding();
    crucible::test::pass("test_opcode_latency_table: 5 groups, all passed\n");
    return 0;
}
