// The header ships its own static_asserts, which run only once some
// translation unit in the build graph includes it under the project's
// warning and standard flags.  This one does that.
//
// Every constexpr accessor is also driven with non-constant arguments, so
// a regression in a fold step surfaces under runtime semantics and not
// only at consteval time.

#include <crucible/mimic/CogMimic.h>

#include "test_assert.h"

#include <cstdint>
#include <cstdio>
#include <type_traits>

namespace cog = crucible::cog;
namespace mimic = crucible::mimic;
namespace effects = crucible::effects;
namespace safety = crucible::safety;

static void test_default_state_runtime() {
    mimic::CogMimic<cog::CogKind::Gpu> gpu_mimic{};
    mimic::CogMimic<cog::CogKind::CpuCore> cpu_core_mimic{};
    mimic::CogMimic<cog::CogKind::CpuSocket> cpu_socket_mimic{};

    // The volatile load defeats constant folding, so the accessor body
    // actually runs.
    volatile bool gpu_uncal = gpu_mimic.is_uncalibrated();
    volatile bool cpu_core_uncal = cpu_core_mimic.is_uncalibrated();
    volatile bool cpu_sock_uncal = cpu_socket_mimic.is_uncalibrated();
    assert(gpu_uncal);
    assert(cpu_core_uncal);
    assert(cpu_sock_uncal);

    assert(gpu_mimic.identity == nullptr);
    assert(cpu_core_mimic.identity == nullptr);
    assert(cpu_socket_mimic.identity == nullptr);

    assert(gpu_mimic.opcode_latency_table.empty());
    assert(cpu_core_mimic.opcode_latency_table.empty());
    assert(cpu_socket_mimic.opcode_latency_table.empty());

    static_assert(mimic::CogMimic<cog::CogKind::Gpu>::kind == cog::CogKind::Gpu);

    std::printf("  test_default_state_runtime:           PASSED\n");
}

static void test_target_caps_class_hash_determinism() {
    mimic::CogMimic<cog::CogKind::Gpu> a{};
    mimic::CogMimic<cog::CogKind::Gpu> b{};
    volatile std::uint64_t ha = a.target_caps_class_hash();
    volatile std::uint64_t hb = b.target_caps_class_hash();
    assert(ha == hb);

    // The kind's underlying value seeds the high byte, so two kinds
    // cannot land on one hash.
    mimic::CogMimic<cog::CogKind::CpuCore> c{};
    volatile std::uint64_t hc = c.target_caps_class_hash();
    assert(ha != hc);

    mimic::CogMimic<cog::CogKind::CpuSocket> s{};
    volatile std::uint64_t hs = s.target_caps_class_hash();
    assert(ha != hs);
    assert(hc != hs);

    std::printf("  test_target_caps_class_hash_determinism: PASSED\n");
}

// Two GPU generations must produce different federation hashes, or a
// kernel compiled for one silently reuses on the other.

static void test_target_caps_class_hash_sm_version_discrimination() {
    mimic::CogMimic<cog::CogKind::Gpu> hopper{};
    hopper.calibrated_caps.value_mut().sm_version =
        safety::Tagged<std::uint16_t, safety::source::Vendor>{std::uint16_t{90}};
    hopper.calibrated_caps.value_mut().sm_count =
        safety::Tagged<std::uint16_t, safety::source::Vendor>{std::uint16_t{132}};

    mimic::CogMimic<cog::CogKind::Gpu> blackwell{};
    blackwell.calibrated_caps.value_mut().sm_version =
        safety::Tagged<std::uint16_t, safety::source::Vendor>{std::uint16_t{100}};
    blackwell.calibrated_caps.value_mut().sm_count =
        safety::Tagged<std::uint16_t, safety::source::Vendor>{std::uint16_t{208}};

    volatile std::uint64_t hopper_hash = hopper.target_caps_class_hash();
    volatile std::uint64_t blackwell_hash = blackwell.target_caps_class_hash();
    assert(hopper_hash != blackwell_hash);

    mimic::CogMimic<cog::CogKind::Gpu> hopper_twin = hopper;
    volatile std::uint64_t twin_hash = hopper_twin.target_caps_class_hash();
    assert(hopper_hash == twin_hash);

    std::printf("  test_target_caps_class_hash_sm_version_discrimination: PASSED\n");
}

// One physical Cog that takes a firmware update gets a new per-Cog cache
// slot and keeps its federation slot.  Without the rotation, kernel
// binaries alias across firmware revisions.

static void test_cog_kernel_cache_key_firmware_rotation() {
    cog::CogIdentity id_v1{};
    id_v1.uuid = cog::Uuid{0xAA000001ULL, 0xBB000002ULL};
    id_v1.kind = cog::CogKind::Gpu;
    id_v1.firmware_revision = safety::Tagged<std::uint64_t, safety::source::Vendor>{1};
    id_v1.bios_revision = safety::Tagged<std::uint64_t, safety::source::Vendor>{42};

    cog::CogIdentity id_v2 = id_v1;
    id_v2.firmware_revision = safety::Tagged<std::uint64_t, safety::source::Vendor>{2};

    cog::GpuTargetCaps caps{};
    caps.sm_version = safety::Tagged<std::uint16_t, safety::source::Vendor>{std::uint16_t{90}};
    caps.sm_count = safety::Tagged<std::uint16_t, safety::source::Vendor>{std::uint16_t{132}};

    mimic::CogMimic<cog::CogKind::Gpu> mimic_v1{};
    mimic_v1.identity = &id_v1;
    mimic_v1.calibrated_caps = safety::Tagged<cog::GpuTargetCaps, safety::source::Calibrated>{caps};

    mimic::CogMimic<cog::CogKind::Gpu> mimic_v2{};
    mimic_v2.identity = &id_v2;
    mimic_v2.calibrated_caps = safety::Tagged<cog::GpuTargetCaps, safety::source::Calibrated>{caps};

    volatile std::uint64_t fed_v1 = mimic_v1.target_caps_class_hash();
    volatile std::uint64_t fed_v2 = mimic_v2.target_caps_class_hash();
    assert(fed_v1 == fed_v2);

    volatile std::uint64_t cog_v1 = mimic_v1.cog_kernel_cache_key();
    volatile std::uint64_t cog_v2 = mimic_v2.cog_kernel_cache_key();
    assert(cog_v1 != cog_v2);

    cog::CogIdentity id_bios_drift = id_v1;
    id_bios_drift.bios_revision = safety::Tagged<std::uint64_t, safety::source::Vendor>{43};

    mimic::CogMimic<cog::CogKind::Gpu> mimic_bios_drift{};
    mimic_bios_drift.identity = &id_bios_drift;
    mimic_bios_drift.calibrated_caps = mimic_v1.calibrated_caps;

    volatile std::uint64_t cog_bios_drift = mimic_bios_drift.cog_kernel_cache_key();
    assert(cog_bios_drift != cog_v1);

    volatile std::uint64_t fed_bios_drift = mimic_bios_drift.target_caps_class_hash();
    assert(fed_bios_drift == fed_v1);

    std::printf("  test_cog_kernel_cache_key_firmware_rotation: PASSED\n");
}

static void test_mint_cog_mimic_round_trip() {
    cog::CogIdentity id{};
    id.uuid = cog::Uuid{0xCAFEBABEULL, 0xDEADBEEFULL};
    id.kind = cog::CogKind::Gpu;
    id.firmware_revision = safety::Tagged<std::uint64_t, safety::source::Vendor>{0xABCDULL};
    id.bios_revision = safety::Tagged<std::uint64_t, safety::source::Vendor>{0x1234ULL};

    cog::GpuTargetCaps caps{};
    caps.sm_version = safety::Tagged<std::uint16_t, safety::source::Vendor>{std::uint16_t{90}};
    caps.sm_count = safety::Tagged<std::uint16_t, safety::source::Vendor>{std::uint16_t{132}};

    cog::OpcodeLatencyTable<cog::CogKind::Gpu> tbl{};

    using InitCtx = effects::ExecCtx<effects::Init, effects::ctx_numa::Any, effects::ctx_alloc::Unbound,
                                     effects::ctx_heat::Cold, effects::ctx_resid::DRAM,
                                     effects::Row<effects::Effect::Init>, effects::ctx_workload::Unspecified>;

    InitCtx ctx{};
    auto m = mimic::mint_cog_mimic<cog::CogKind::Gpu>(ctx, id, caps, tbl);

    assert(m.identity == &id);
    assert(m.identity->kind == cog::CogKind::Gpu);

    volatile std::uint16_t round_trip_sm_version = m.calibrated_caps.value().sm_version.value();
    volatile std::uint16_t round_trip_sm_count = m.calibrated_caps.value().sm_count.value();
    assert(round_trip_sm_version == 90);
    assert(round_trip_sm_count == 132);

    assert(m.opcode_latency_table.empty());

    // The identity is bound, but an empty opcode table still counts as
    // uncalibrated.
    volatile bool uncal = m.is_uncalibrated();
    assert(uncal);

    // The per-Cog key needs a non-null identity holding a non-zero uuid,
    // which the mint has now supplied.
    volatile std::uint64_t per_cog_key = m.cog_kernel_cache_key();
    volatile std::uint64_t federation = m.target_caps_class_hash();
    assert(per_cog_key != federation);  // fmix64(fed XOR content_hash) ≠ fed
    assert(per_cog_key != 0);
    assert(federation != 0);

    using BgCtx =
        effects::ExecCtx<effects::Bg, effects::ctx_numa::Any, effects::ctx_alloc::Arena, effects::ctx_heat::Warm,
                         effects::ctx_resid::L3, effects::Row<effects::Effect::Bg, effects::Effect::Alloc>,
                         effects::ctx_workload::Unspecified>;

    BgCtx bg_ctx{};
    auto m_bg = mimic::mint_cog_mimic<cog::CogKind::Gpu>(bg_ctx, id, caps, tbl);
    assert(m_bg.identity == &id);
    volatile std::uint64_t bg_per_cog_key = m_bg.cog_kernel_cache_key();
    assert(bg_per_cog_key == per_cog_key);  // same identity → same key

    std::printf("  test_mint_cog_mimic_round_trip:       PASSED\n");
}

// All three admitted compute kinds flow through one factory.  Driving
// each explicitly catches a specialization whose projection fold breaks
// in isolation.

static void test_mint_cpu_paths() {
    cog::CogIdentity cpu_id{};
    cpu_id.uuid = cog::Uuid{0x1ULL, 0x2ULL};
    cpu_id.kind = cog::CogKind::CpuCore;
    cpu_id.firmware_revision = safety::Tagged<std::uint64_t, safety::source::Vendor>{0xC0DEULL};

    cog::CpuCoreTargetCaps cpu_caps{};
    cpu_caps.base_clock_mhz = safety::Tagged<std::uint32_t, safety::source::Vendor>{2500U};
    cpu_caps.l2_bytes = safety::Tagged<std::uint32_t, safety::source::Vendor>{1U * 1024 * 1024};

    using InitCtx = effects::ExecCtx<effects::Init, effects::ctx_numa::Any, effects::ctx_alloc::Unbound,
                                     effects::ctx_heat::Cold, effects::ctx_resid::DRAM,
                                     effects::Row<effects::Effect::Init>, effects::ctx_workload::Unspecified>;
    InitCtx ctx{};

    auto cpu_core_mimic = mimic::mint_cog_mimic<cog::CogKind::CpuCore>(
        ctx, cpu_id, cpu_caps, cog::OpcodeLatencyTable<cog::CogKind::CpuCore>{});
    assert(cpu_core_mimic.identity == &cpu_id);
    volatile std::uint64_t cpu_core_fed = cpu_core_mimic.target_caps_class_hash();
    assert(cpu_core_fed != 0);

    cog::CogIdentity sock_id{};
    sock_id.uuid = cog::Uuid{0x100ULL, 0x200ULL};
    sock_id.kind = cog::CogKind::CpuSocket;

    cog::CpuSocketTargetCaps sock_caps{};
    sock_caps.core_count = safety::Tagged<std::uint16_t, safety::source::Vendor>{std::uint16_t{96}};
    sock_caps.l3_bytes = safety::Tagged<std::uint64_t, safety::source::Vendor>{std::uint64_t{384U} * 1024 * 1024};

    auto sock_mimic = mimic::mint_cog_mimic<cog::CogKind::CpuSocket>(
        ctx, sock_id, sock_caps, cog::OpcodeLatencyTable<cog::CogKind::CpuSocket>{});
    assert(sock_mimic.identity == &sock_id);
    volatile std::uint64_t sock_fed = sock_mimic.target_caps_class_hash();
    assert(sock_fed != 0);

    assert(cpu_core_fed != sock_fed);

    std::printf("  test_mint_cpu_paths:                  PASSED\n");
}

// The header asserts admission at compile time.  Reading the same
// constants at runtime catches a refactor that drifts those assertions
// out of agreement with how the concept actually instantiates.

static void test_ctx_fits_cog_mimic_concept_gate_runtime() {
    using InitCtx = effects::ExecCtx<effects::Init, effects::ctx_numa::Any, effects::ctx_alloc::Unbound,
                                     effects::ctx_heat::Cold, effects::ctx_resid::DRAM,
                                     effects::Row<effects::Effect::Init>, effects::ctx_workload::Unspecified>;

    using TestCtx = effects::ExecCtx<effects::Test, effects::ctx_numa::Any, effects::ctx_alloc::Stack,
                                     effects::ctx_heat::Cold, effects::ctx_resid::DRAM,
                                     effects::Row<effects::Effect::Test>, effects::ctx_workload::Unspecified>;

    volatile bool init_admits_gpu = mimic::CtxFitsCogMimic<InitCtx, cog::CogKind::Gpu>;
    volatile bool init_admits_cpu_core = mimic::CtxFitsCogMimic<InitCtx, cog::CogKind::CpuCore>;
    volatile bool init_admits_nic = mimic::CtxFitsCogMimic<InitCtx, cog::CogKind::NicPort>;
    volatile bool init_admits_dram = mimic::CtxFitsCogMimic<InitCtx, cog::CogKind::DramChannel>;
    volatile bool init_admits_psu = mimic::CtxFitsCogMimic<InitCtx, cog::CogKind::PsuRail>;
    volatile bool init_admits_dc = mimic::CtxFitsCogMimic<InitCtx, cog::CogKind::Datacenter>;
    volatile bool test_admits_gpu = mimic::CtxFitsCogMimic<TestCtx, cog::CogKind::Gpu>;

    // An Init context admits every substrate family: compute, network
    // and memory.
    assert(init_admits_gpu);
    assert(init_admits_cpu_core);
    assert(init_admits_nic);
    assert(init_admits_dram);

    // The power and container families have no Mimic instance, so an
    // Init context refuses them.
    assert(!init_admits_psu);
    assert(!init_admits_dc);

    // A Test context carries neither Init nor Bg in its row.
    assert(!test_admits_gpu);

    std::printf("  test_ctx_fits_cog_mimic_concept_gate_runtime: PASSED\n");
}

static void test_trivially_destructible_carrier() {
    // The carrier owns no heap, so destruction must stay trivial.
    static_assert(std::is_trivially_destructible_v<mimic::CogMimic<cog::CogKind::Gpu>>);
    static_assert(std::is_trivially_destructible_v<mimic::CogMimic<cog::CogKind::CpuCore>>);
    static_assert(std::is_trivially_destructible_v<mimic::CogMimic<cog::CogKind::CpuSocket>>);

    std::printf("  test_trivially_destructible_carrier:  PASSED\n");
}

int main() {
    std::printf("test_cog_mimic:\n");
    test_default_state_runtime();
    test_target_caps_class_hash_determinism();
    test_target_caps_class_hash_sm_version_discrimination();
    test_cog_kernel_cache_key_firmware_rotation();
    test_mint_cog_mimic_round_trip();
    test_mint_cpu_paths();
    test_ctx_fits_cog_mimic_concept_gate_runtime();
    test_trivially_destructible_carrier();
    std::printf("test_cog_mimic: all PASSED\n");
    return 0;
}
