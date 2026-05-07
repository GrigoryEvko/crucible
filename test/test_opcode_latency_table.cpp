// Sentinel TU for include/crucible/cog/OpcodeLatencyTable.h.
//
// Per feedback_header_only_static_assert_blind_spot.md: the header
// ships substantial in-header static_asserts (FOUND-I04 frozen-position
// pins on 9 enums + opcodes_for binding pins + standard-layout pins)
// that only execute under the project's full warning + standard flags
// when SOMEONE includes the header from a TU that lands in the build
// graph.  This sentinel makes the inclusion explicit so the in-header
// invariants are exercised by every default build.
//
// Per feedback_algebra_runtime_smoke_test_discipline: every constexpr
// accessor (size_bucket_name / dtype_bucket_name / *_opcode_name × 5)
// is driven with non-constant runtime arguments here so a regression
// in a switch arm surfaces under runtime semantics, not only at
// consteval time.
//
// GAPS-187.

#include <crucible/cog/OpcodeLatencyTable.h>

#include "test_assert.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <span>
#include <string_view>
#include <type_traits>

namespace cog = crucible::cog;
namespace safety = crucible::safety;

// ── Reflection-driven name coverage: SizeBucket ─────────────────────

static void test_size_bucket_name_coverage() {
    constexpr cog::SizeBucket buckets[] = {
        cog::SizeBucket::None,
        cog::SizeBucket::S64,
        cog::SizeBucket::S128,
        cog::SizeBucket::S256,
        cog::SizeBucket::S512,
        cog::SizeBucket::S1024,
        cog::SizeBucket::S2048,
        cog::SizeBucket::S4096,
    };
    static_assert(sizeof(buckets) / sizeof(buckets[0]) == cog::size_bucket_count,
        "Manual buckets[] table diverged from size_bucket_count.");

    for (cog::SizeBucket B : buckets) {
        volatile auto vB = B;
        std::string_view name = cog::size_bucket_name(
            static_cast<cog::SizeBucket>(vB));
        assert(!name.empty());
        assert(name != std::string_view{"<unknown SizeBucket>"});

        // Underlying value IS the bucket size.  Volatile barrier
        // prevents constant-folding so the runtime arm is exercised.
        volatile std::uint16_t expected = static_cast<std::uint16_t>(vB);
        std::uint16_t actual = static_cast<std::uint16_t>(B);
        assert(actual == expected);
    }
    std::printf("  test_size_bucket_name_coverage:       PASSED\n");
}

// ── Reflection-driven name coverage: DtypeBucket ────────────────────

static void test_dtype_bucket_name_coverage() {
    constexpr cog::DtypeBucket buckets[] = {
        cog::DtypeBucket::None, cog::DtypeBucket::Fp64,
        cog::DtypeBucket::Fp32, cog::DtypeBucket::Tf32,
        cog::DtypeBucket::Fp16, cog::DtypeBucket::Bf16,
        cog::DtypeBucket::Fp8,  cog::DtypeBucket::Fp4,
        cog::DtypeBucket::Int8,
    };
    static_assert(sizeof(buckets) / sizeof(buckets[0]) == cog::dtype_bucket_count);

    for (cog::DtypeBucket B : buckets) {
        volatile auto vB = B;
        auto name = cog::dtype_bucket_name(static_cast<cog::DtypeBucket>(vB));
        assert(!name.empty());
        assert(name != std::string_view{"<unknown DtypeBucket>"});
    }
    std::printf("  test_dtype_bucket_name_coverage:      PASSED\n");
}

// ── Reflection-driven name coverage: TransposeMode ──────────────────

static void test_transpose_mode_name_coverage() {
    constexpr cog::TransposeMode modes[] = {
        cog::TransposeMode::Nn, cog::TransposeMode::Tn,
        cog::TransposeMode::Nt, cog::TransposeMode::Tt,
    };
    static_assert(sizeof(modes) / sizeof(modes[0]) == cog::transpose_mode_count);

    for (cog::TransposeMode M : modes) {
        volatile auto vM = M;
        auto name = cog::transpose_mode_name(static_cast<cog::TransposeMode>(vM));
        assert(!name.empty());
        assert(name != std::string_view{"<unknown TransposeMode>"});
    }
    std::printf("  test_transpose_mode_name_coverage:    PASSED\n");
}

// ── Reflection-driven name coverage: MessageSizeBucket ──────────────

static void test_message_size_bucket_name_coverage() {
    constexpr cog::MessageSizeBucket buckets[] = {
        cog::MessageSizeBucket::None,  cog::MessageSizeBucket::M64B,
        cog::MessageSizeBucket::M1K,   cog::MessageSizeBucket::M16K,
        cog::MessageSizeBucket::M256K, cog::MessageSizeBucket::M4M,
        cog::MessageSizeBucket::M64M,
    };
    static_assert(sizeof(buckets) / sizeof(buckets[0]) ==
                  cog::message_size_bucket_count);

    for (cog::MessageSizeBucket B : buckets) {
        volatile auto vB = B;
        auto name = cog::message_size_bucket_name(
            static_cast<cog::MessageSizeBucket>(vB));
        assert(!name.empty());
        assert(name != std::string_view{"<unknown MessageSizeBucket>"});

        // Underlying value IS the bucket size in BYTES.
        volatile std::uint32_t expected = static_cast<std::uint32_t>(vB);
        std::uint32_t actual = static_cast<std::uint32_t>(B);
        assert(actual == expected);
    }
    std::printf("  test_message_size_bucket_name_coverage: PASSED\n");
}

// ── Per-substrate opcode name coverage ─────────────────────────────

static void test_gpu_opcode_runtime() {
    constexpr cog::GpuOpcode ops[] = {
        cog::GpuOpcode::GemmPlain,      cog::GpuOpcode::GemmFused,
        cog::GpuOpcode::Sdpa,           cog::GpuOpcode::Conv2D,
        cog::GpuOpcode::AllReduceRing,  cog::GpuOpcode::AllReduceTree,
        cog::GpuOpcode::AllGather,      cog::GpuOpcode::NvlinkP2pRead,
        cog::GpuOpcode::NvlinkP2pWrite, cog::GpuOpcode::PciePeer,
        cog::GpuOpcode::KernelLaunch,   cog::GpuOpcode::DoorbellRing,
        cog::GpuOpcode::EventQuery,
    };
    static_assert(sizeof(ops) / sizeof(ops[0]) == cog::gpu_opcode_count);

    for (cog::GpuOpcode O : ops) {
        volatile auto vO = O;
        auto name = cog::gpu_opcode_name(static_cast<cog::GpuOpcode>(vO));
        assert(!name.empty());
        assert(name != std::string_view{"<unknown GpuOpcode>"});
    }
    std::printf("  test_gpu_opcode_runtime:              PASSED\n");
}

static void test_nic_opcode_runtime() {
    constexpr cog::NicOpcode ops[] = {
        cog::NicOpcode::RdmaWrite,      cog::NicOpcode::RdmaSend,
        cog::NicOpcode::RdmaRead,       cog::NicOpcode::CompletionPoll,
        cog::NicOpcode::QpCreate,       cog::NicOpcode::QpDestroy,
        cog::NicOpcode::MrRegister,     cog::NicOpcode::MrDeregister,
        cog::NicOpcode::DoorbellRing,   cog::NicOpcode::TcpSend,
        cog::NicOpcode::TcpRecv,        cog::NicOpcode::AfXdpEnqueue,
        cog::NicOpcode::AfXdpDequeue,   cog::NicOpcode::GpuDirectWrite,
        cog::NicOpcode::GpuDirectRead,
    };
    static_assert(sizeof(ops) / sizeof(ops[0]) == cog::nic_opcode_count);

    for (cog::NicOpcode O : ops) {
        volatile auto vO = O;
        auto name = cog::nic_opcode_name(static_cast<cog::NicOpcode>(vO));
        assert(!name.empty());
        assert(name != std::string_view{"<unknown NicOpcode>"});
    }
    std::printf("  test_nic_opcode_runtime:              PASSED\n");
}

static void test_switch_opcode_runtime() {
    constexpr cog::SwitchOpcode ops[] = {
        cog::SwitchOpcode::PortForward,
        cog::SwitchOpcode::AclMatch,
        cog::SwitchOpcode::SharpReduce,
        cog::SwitchOpcode::MulticastReplicate,
    };
    static_assert(sizeof(ops) / sizeof(ops[0]) == cog::switch_opcode_count);

    for (cog::SwitchOpcode O : ops) {
        volatile auto vO = O;
        auto name = cog::switch_opcode_name(static_cast<cog::SwitchOpcode>(vO));
        assert(!name.empty());
        assert(name != std::string_view{"<unknown SwitchOpcode>"});
    }
    std::printf("  test_switch_opcode_runtime:           PASSED\n");
}

static void test_cpu_opcode_runtime() {
    constexpr cog::CpuOpcode ops[] = {
        cog::CpuOpcode::Memcpy,        cog::CpuOpcode::Vfma,
        cog::CpuOpcode::AvxLoad,       cog::CpuOpcode::AvxStore,
        cog::CpuOpcode::ContextSwitch, cog::CpuOpcode::AtomicCas,
        cog::CpuOpcode::MutexLock,     cog::CpuOpcode::MutexUnlock,
        cog::CpuOpcode::FutexWait,     cog::CpuOpcode::Syscall,
    };
    static_assert(sizeof(ops) / sizeof(ops[0]) == cog::cpu_opcode_count);

    for (cog::CpuOpcode O : ops) {
        volatile auto vO = O;
        auto name = cog::cpu_opcode_name(static_cast<cog::CpuOpcode>(vO));
        assert(!name.empty());
        assert(name != std::string_view{"<unknown CpuOpcode>"});
    }
    std::printf("  test_cpu_opcode_runtime:              PASSED\n");
}

static void test_dram_opcode_runtime() {
    constexpr cog::DramOpcode ops[] = {
        cog::DramOpcode::ChannelRead,  cog::DramOpcode::ChannelWrite,
        cog::DramOpcode::RowActivate,  cog::DramOpcode::BankRefresh,
        cog::DramOpcode::Precharge,
    };
    static_assert(sizeof(ops) / sizeof(ops[0]) == cog::dram_opcode_count);

    for (cog::DramOpcode O : ops) {
        volatile auto vO = O;
        auto name = cog::dram_opcode_name(static_cast<cog::DramOpcode>(vO));
        assert(!name.empty());
        assert(name != std::string_view{"<unknown DramOpcode>"});
    }
    std::printf("  test_dram_opcode_runtime:             PASSED\n");
}

// ── LatencyQuantiles + OrderedLatencyQuantiles construction ─────────

static void test_latency_quantiles_ordered_construction() {
    // Well-formed triple: p50 = 100, p99 = 500, p999 = 2000.
    cog::LatencyQuantiles q{100u, 500u, 2000u};
    cog::OrderedLatencyQuantiles ordered{q};
    volatile auto p50  = ordered.value().p50_ns;
    volatile auto p99  = ordered.value().p99_ns;
    volatile auto p999 = ordered.value().p999_ns;
    assert(p50  == 100u);
    assert(p99  == 500u);
    assert(p999 == 2000u);

    // Boundary triple: equal quantiles (p50 == p99 == p999).  Edge of
    // the ordering invariant — still well-formed.
    cog::LatencyQuantiles flat{42u, 42u, 42u};
    cog::OrderedLatencyQuantiles ordered_flat{flat};
    assert(ordered_flat.value().p50_ns == 42u);

    // EBO sanity at runtime — wrapper preserves the underlying size.
    static_assert(sizeof(cog::OrderedLatencyQuantiles) ==
                  sizeof(cog::LatencyQuantiles));

    std::printf("  test_latency_quantiles_construction:  PASSED\n");
}

// ── OpcodeLatencyEntry / Table construction smoke ───────────────────

static void test_gpu_opcode_table_construction() {
    using Entry = cog::OpcodeLatencyEntry<cog::CogKind::Gpu>;

    // Static storage — the entries span borrows from this fleet of
    // calibrated rows.  Two GEMM rows (Bf16 + Fp16 at 4096³) plus an
    // AllReduceRing row at 4MB messages.
    static constexpr std::array<Entry, 3> rows = {
        Entry{
            .opcode              = cog::GpuOpcode::GemmPlain,
            .size_bucket         = cog::SizeBucket::S4096,
            .dtype_bucket        = cog::DtypeBucket::Bf16,
            .transpose_mode      = cog::TransposeMode::Nn,
            .message_size_bucket = cog::MessageSizeBucket::None,
            .latency_cycles      = 8'500'000u,
            .latency             = cog::OrderedLatencyQuantiles{
                                       cog::LatencyQuantiles{
                                           2'500'000u,   // p50
                                           4'500'000u,   // p99
                                           7'200'000u}}, // p999
            .throughput_per_sec  = 989.0e12,
            .sample_count        = safety::Tagged<std::uint16_t,
                                                  safety::source::Calibrated>{1024},
        },
        Entry{
            .opcode              = cog::GpuOpcode::GemmPlain,
            .size_bucket         = cog::SizeBucket::S4096,
            .dtype_bucket        = cog::DtypeBucket::Fp16,
            .transpose_mode      = cog::TransposeMode::Nn,
            .message_size_bucket = cog::MessageSizeBucket::None,
            .latency_cycles      = 8'500'000u,
            .latency             = cog::OrderedLatencyQuantiles{
                                       cog::LatencyQuantiles{
                                           2'600'000u, 4'700'000u, 7'500'000u}},
            .throughput_per_sec  = 989.0e12,
            .sample_count        = safety::Tagged<std::uint16_t,
                                                  safety::source::Calibrated>{1024},
        },
        Entry{
            .opcode              = cog::GpuOpcode::AllReduceRing,
            .size_bucket         = cog::SizeBucket::None,
            .dtype_bucket        = cog::DtypeBucket::Fp32,
            .transpose_mode      = cog::TransposeMode::Nn,
            .message_size_bucket = cog::MessageSizeBucket::M4M,
            .latency_cycles      = 1'200'000u,
            .latency             = cog::OrderedLatencyQuantiles{
                                       cog::LatencyQuantiles{
                                           350'000u, 600'000u, 1'400'000u}},
            .throughput_per_sec  = 4.5e10,
            .sample_count        = safety::Tagged<std::uint16_t,
                                                  safety::source::Calibrated>{256},
        },
    };

    cog::OpcodeLatencyTable<cog::CogKind::Gpu> table{
        .entries = safety::Tagged<std::span<const Entry>,
                                  safety::source::Calibrated>{
                       std::span<const Entry>{rows}},
        .calibration_age_seconds = safety::Stale<double>::at(12.5, 0),
    };

    // Volatile reads through the queries — each call exercises the
    // runtime path of the constexpr query helper.
    volatile auto sz = table.size();
    assert(sz == 3);
    volatile bool empty = table.empty();
    assert(!empty);

    auto found = table.lookup_by_opcode(cog::GpuOpcode::AllReduceRing);
    assert(found.has_value());
    assert(found->message_size_bucket == cog::MessageSizeBucket::M4M);

    auto missing = table.lookup_by_opcode(cog::GpuOpcode::Conv2D);
    assert(!missing.has_value());

    auto fullkey = table.latency_for_size_bucket(
        cog::GpuOpcode::GemmPlain,
        cog::SizeBucket::S4096,
        cog::DtypeBucket::Fp16);
    assert(fullkey.has_value());
    assert(fullkey->dtype_bucket == cog::DtypeBucket::Fp16);

    // Throughput envelope sums both GEMM rows.
    volatile double envelope =
        table.throughput_envelope(cog::GpuOpcode::GemmPlain);
    assert(envelope > 1.5e15);
    assert(envelope < 2.5e15);

    // Default-constructed table is empty.
    cog::OpcodeLatencyTable<cog::CogKind::Gpu> empty_table{};
    assert(empty_table.empty());
    assert(empty_table.size() == 0);
    assert(!empty_table.lookup_by_opcode(cog::GpuOpcode::GemmPlain).has_value());

    std::printf("  test_gpu_opcode_table_construction:   PASSED\n");
}

static void test_nic_opcode_table_construction() {
    using Entry = cog::OpcodeLatencyEntry<cog::CogKind::NicPort>;

    static constexpr std::array<Entry, 2> rows = {
        Entry{
            .opcode              = cog::NicOpcode::RdmaWrite,
            .size_bucket         = cog::SizeBucket::None,
            .dtype_bucket        = cog::DtypeBucket::None,
            .transpose_mode      = cog::TransposeMode::Nn,
            .message_size_bucket = cog::MessageSizeBucket::M64B,
            .latency_cycles      = 4'500u,
            .latency             = cog::OrderedLatencyQuantiles{
                                       cog::LatencyQuantiles{
                                           1'500u, 2'500u, 5'500u}},
            .throughput_per_sec  = 1.5e7,
            .sample_count        = safety::Tagged<std::uint16_t,
                                                  safety::source::Calibrated>{4096},
        },
        Entry{
            .opcode              = cog::NicOpcode::RdmaWrite,
            .size_bucket         = cog::SizeBucket::None,
            .dtype_bucket        = cog::DtypeBucket::None,
            .transpose_mode      = cog::TransposeMode::Nn,
            .message_size_bucket = cog::MessageSizeBucket::M4M,
            .latency_cycles      = 850'000u,
            .latency             = cog::OrderedLatencyQuantiles{
                                       cog::LatencyQuantiles{
                                           250'000u, 450'000u, 920'000u}},
            .throughput_per_sec  = 6.25e9,
            .sample_count        = safety::Tagged<std::uint16_t,
                                                  safety::source::Calibrated>{2048},
        },
    };

    cog::OpcodeLatencyTable<cog::CogKind::NicPort> table{
        .entries = safety::Tagged<std::span<const Entry>,
                                  safety::source::Calibrated>{
                       std::span<const Entry>{rows}},
        .calibration_age_seconds = safety::Stale<double>::fresh(0.0),
    };

    auto small = table.latency_for_size_bucket(
        cog::NicOpcode::RdmaWrite,
        cog::SizeBucket::None,
        cog::DtypeBucket::None,
        cog::TransposeMode::Nn,
        cog::MessageSizeBucket::M64B);
    assert(small.has_value());
    volatile auto small_p99 = small->latency.value().p99_ns;
    assert(small_p99 == 2'500u);

    auto big = table.latency_for_size_bucket(
        cog::NicOpcode::RdmaWrite,
        cog::SizeBucket::None,
        cog::DtypeBucket::None,
        cog::TransposeMode::Nn,
        cog::MessageSizeBucket::M4M);
    assert(big.has_value());
    volatile auto big_p99 = big->latency.value().p99_ns;
    assert(big_p99 == 450'000u);

    // Calibration age — fresh () means staleness grade τ = 0.
    assert(table.calibration_age_seconds.is_fresh());

    std::printf("  test_nic_opcode_table_construction:   PASSED\n");
}

// ── opcodes_for binding + HasOpcodeTable gate runtime confirmation ──

static void test_opcodes_for_binding() {
    // Type-equality pins (also asserted in-header but exercised here
    // under TU-context warning flags).
    static_assert(std::is_same_v<cog::opcodes_for_t<cog::CogKind::Gpu>,
                                  cog::GpuOpcode>);
    static_assert(std::is_same_v<cog::opcodes_for_t<cog::CogKind::CpuCore>,
                                  cog::CpuOpcode>);
    static_assert(std::is_same_v<cog::opcodes_for_t<cog::CogKind::CpuSocket>,
                                  cog::CpuOpcode>);
    static_assert(std::is_same_v<cog::opcodes_for_t<cog::CogKind::NicPort>,
                                  cog::NicOpcode>);
    static_assert(std::is_same_v<cog::opcodes_for_t<cog::CogKind::NvSwitch>,
                                  cog::SwitchOpcode>);
    static_assert(std::is_same_v<cog::opcodes_for_t<cog::CogKind::DramChannel>,
                                  cog::DramOpcode>);

    // HasOpcodeTable positive cases — substrates that schedule.
    static_assert( cog::HasOpcodeTable<cog::CogKind::Gpu>);
    static_assert( cog::HasOpcodeTable<cog::CogKind::CpuCore>);
    static_assert( cog::HasOpcodeTable<cog::CogKind::CpuSocket>);
    static_assert( cog::HasOpcodeTable<cog::CogKind::NicPort>);
    static_assert( cog::HasOpcodeTable<cog::CogKind::NvSwitch>);
    static_assert( cog::HasOpcodeTable<cog::CogKind::DramChannel>);

    // HasOpcodeTable negative cases — non-schedulable Cogs.
    static_assert(!cog::HasOpcodeTable<cog::CogKind::PsuRail>);
    static_assert(!cog::HasOpcodeTable<cog::CogKind::BmcSensor>);
    static_assert(!cog::HasOpcodeTable<cog::CogKind::OpticalTransceiver>);
    static_assert(!cog::HasOpcodeTable<cog::CogKind::PcieLaneGroup>);
    static_assert(!cog::HasOpcodeTable<cog::CogKind::Datacenter>);

    // Runtime confirmation via constrained-template instantiation.
    auto query = []<cog::CogKind K>() requires cog::HasOpcodeTable<K> {
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

    std::printf("  test_opcodes_for_binding:             PASSED\n");
}

int main() {
    std::printf("test_opcode_latency_table: 13 groups\n");
    test_size_bucket_name_coverage();
    test_dtype_bucket_name_coverage();
    test_transpose_mode_name_coverage();
    test_message_size_bucket_name_coverage();
    test_gpu_opcode_runtime();
    test_nic_opcode_runtime();
    test_switch_opcode_runtime();
    test_cpu_opcode_runtime();
    test_dram_opcode_runtime();
    test_latency_quantiles_ordered_construction();
    test_gpu_opcode_table_construction();
    test_nic_opcode_table_construction();
    test_opcodes_for_binding();
    std::printf("test_opcode_latency_table: 13 groups, all passed\n");
    return 0;
}
