// The header ships its own static_asserts, which run only once some
// translation unit in the build graph includes it under the project's
// warning and standard flags.  This one does that.
//
// A CogMimic comes only from mint_cog_mimic, and a header cannot take a
// context from the test doors, so the checks that need a minted value
// live here.  Each runs once at compile time and once with values the
// compiler cannot fold, so a regression in a fold step surfaces under
// runtime semantics and not only at consteval time.

#include <crucible/mimic/CogMimic.h>
#include <fixy/Ctx.h>
#include <foundation/effects/Effect.h>

#include "test_assert.h"

#include <cstdint>
#include <cstdio>
#include <type_traits>

namespace cog = crucible::cog;
namespace mimic = crucible::mimic;

namespace {

// The contexts a mint accepts, built through the test doors.
constexpr ::fixy::ColdInitCtx init_context() { return ::fixy::ColdInitCtx{::foundation::effects::testing::init()}; }

constexpr ::fixy::BgDrainCtx background_context() { return ::fixy::BgDrainCtx{::foundation::effects::testing::bg()}; }

constexpr cog::GpuTargetCaps gpu_caps(std::uint16_t sm_version, std::uint16_t sm_count) {
    cog::GpuTargetCaps caps{};
    caps.sm_version = ::fixy::mint_tagged<::fixy::tags::source::Vendor, std::uint16_t>(sm_version);
    caps.sm_count = ::fixy::mint_tagged<::fixy::tags::source::Vendor, std::uint16_t>(sm_count);
    return caps;
}

template <cog::CogKind Kind>
constexpr cog::CogIdentity identity_of(std::uint64_t low, std::uint64_t firmware) {
    cog::CogIdentity identity{};
    identity.uuid = cog::Uuid{0xAA000000ULL, low};
    identity.kind = Kind;
    identity.firmware_revision = ::fixy::mint_tagged<::fixy::tags::source::Vendor, std::uint64_t>(firmware);
    return identity;
}

template <cog::CogKind Kind>
constexpr mimic::CogMimic<Kind> mint_with_default_caps(cog::CogIdentity const& identity) {
    return mimic::mint_cog_mimic<Kind>(init_context(), identity, cog::caps_for_t<Kind>{},
                                       cog::OpcodeLatencyTable<Kind>{});
}

constexpr mimic::CogMimic<cog::CogKind::Gpu> mint_gpu(cog::CogIdentity const& identity, cog::GpuTargetCaps caps) {
    return mimic::mint_cog_mimic<cog::CogKind::Gpu>(init_context(), identity, caps,
                                                    cog::OpcodeLatencyTable<cog::CogKind::Gpu>{});
}

// Firmware drift rotates the per-Cog key and leaves the federation key
// stable, at compile time as well as at run time.
static_assert(
    [] {
        cog::CogIdentity const id_a = identity_of<cog::CogKind::Gpu>(1, 1);
        cog::CogIdentity const id_b = identity_of<cog::CogKind::Gpu>(1, 2);
        auto const a = mint_gpu(id_a, gpu_caps(90, 132));
        auto const b = mint_gpu(id_b, gpu_caps(90, 132));
        return a.cog_kernel_cache_key() != b.cog_kernel_cache_key()
            && a.target_caps_class_hash() == b.target_caps_class_hash();
    }(),
    "Firmware drift must rotate cog_kernel_cache_key and must leave target_caps_class_hash stable.");

// A minted value binds the identity, the caps and the table it was given,
// and an empty table still reads as uncalibrated.
static_assert(
    [] {
        cog::CogIdentity const id = identity_of<cog::CogKind::NicPort>(2, 42);
        cog::NicPortTargetCaps caps{};
        caps.link_layer = ::fixy::mint_tagged<::fixy::tags::source::Vendor, cog::LinkLayer>(cog::LinkLayer::Roce);
        auto const m = mimic::mint_cog_mimic<cog::CogKind::NicPort>(init_context(), id, caps,
                                                                    cog::OpcodeLatencyTable<cog::CogKind::NicPort>{});
        return &m.identity() == &id && m.calibrated_caps().value().link_layer.value() == cog::LinkLayer::Roce
            && m.opcode_latency_table().empty() && m.is_uncalibrated();
    }(),
    "mint_cog_mimic must bind the identity, the caps and the opcode table it is given.");

// The two assignments are written by hand, so each must carry every field.
static_assert(
    [] {
        cog::CogIdentity const id_a = identity_of<cog::CogKind::Gpu>(3, 1);
        cog::CogIdentity const id_b = identity_of<cog::CogKind::Gpu>(4, 1);
        auto target = mint_gpu(id_a, gpu_caps(90, 132));
        auto const source = mint_gpu(id_b, gpu_caps(100, 208));
        target = source;
        bool const copied = &target.identity() == &id_b && target.calibrated_caps().value().sm_version.value() == 100
                         && target.target_caps_class_hash() == source.target_caps_class_hash();
        auto moved_into = mint_gpu(id_a, gpu_caps(90, 132));
        moved_into = mint_gpu(id_b, gpu_caps(100, 208));
        return copied && &moved_into.identity() == &id_b
            && moved_into.calibrated_caps().value().sm_count.value() == 208;
    }(),
    "The copy and move assignments of CogMimic must carry the identity, the caps and the opcode table.");

}  // namespace

static void test_fresh_mint_state_runtime() {
    cog::CogIdentity const gpu_id = identity_of<cog::CogKind::Gpu>(10, 1);
    cog::CogIdentity const core_id = identity_of<cog::CogKind::CpuCore>(11, 1);
    cog::CogIdentity const socket_id = identity_of<cog::CogKind::CpuSocket>(12, 1);
    auto const gpu_mimic = mint_with_default_caps<cog::CogKind::Gpu>(gpu_id);
    auto const cpu_core_mimic = mint_with_default_caps<cog::CogKind::CpuCore>(core_id);
    auto const cpu_socket_mimic = mint_with_default_caps<cog::CogKind::CpuSocket>(socket_id);

    // The volatile load defeats constant folding, so the accessor body
    // actually runs.
    volatile bool gpu_uncal = gpu_mimic.is_uncalibrated();
    volatile bool cpu_core_uncal = cpu_core_mimic.is_uncalibrated();
    volatile bool cpu_sock_uncal = cpu_socket_mimic.is_uncalibrated();
    assert(gpu_uncal);
    assert(cpu_core_uncal);
    assert(cpu_sock_uncal);

    assert(&gpu_mimic.identity() == &gpu_id);
    assert(&cpu_core_mimic.identity() == &core_id);
    assert(&cpu_socket_mimic.identity() == &socket_id);

    assert(gpu_mimic.opcode_latency_table().empty());
    assert(cpu_core_mimic.opcode_latency_table().empty());
    assert(cpu_socket_mimic.opcode_latency_table().empty());

    static_assert(mimic::CogMimic<cog::CogKind::Gpu>::kind == cog::CogKind::Gpu);

    std::printf("  test_fresh_mint_state_runtime:        PASSED\n");
}

static void test_target_caps_class_hash_determinism() {
    cog::CogIdentity const id = identity_of<cog::CogKind::Gpu>(20, 1);
    auto const a = mint_with_default_caps<cog::CogKind::Gpu>(id);
    auto const b = mint_with_default_caps<cog::CogKind::Gpu>(id);
    volatile std::uint64_t ha = a.target_caps_class_hash();
    volatile std::uint64_t hb = b.target_caps_class_hash();
    assert(ha == hb);

    // The kind's underlying value seeds the high byte, so two kinds
    // cannot land on one hash.
    cog::CogIdentity const core_id = identity_of<cog::CogKind::CpuCore>(21, 1);
    auto const c = mint_with_default_caps<cog::CogKind::CpuCore>(core_id);
    volatile std::uint64_t hc = c.target_caps_class_hash();
    assert(ha != hc);

    cog::CogIdentity const socket_id = identity_of<cog::CogKind::CpuSocket>(22, 1);
    auto const s = mint_with_default_caps<cog::CogKind::CpuSocket>(socket_id);
    volatile std::uint64_t hs = s.target_caps_class_hash();
    assert(ha != hs);
    assert(hc != hs);

    std::printf("  test_target_caps_class_hash_determinism: PASSED\n");
}

// Two GPU generations must produce different federation hashes, or a
// kernel compiled for one silently reuses on the other.

static void test_target_caps_class_hash_sm_version_discrimination() {
    cog::CogIdentity const hopper_id = identity_of<cog::CogKind::Gpu>(30, 1);
    cog::CogIdentity const blackwell_id = identity_of<cog::CogKind::Gpu>(31, 1);
    auto const hopper = mint_gpu(hopper_id, gpu_caps(90, 132));
    auto const blackwell = mint_gpu(blackwell_id, gpu_caps(100, 208));

    volatile std::uint64_t hopper_hash = hopper.target_caps_class_hash();
    volatile std::uint64_t blackwell_hash = blackwell.target_caps_class_hash();
    assert(hopper_hash != blackwell_hash);

    mimic::CogMimic<cog::CogKind::Gpu> const hopper_twin = hopper;
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
    id_v1.firmware_revision = ::fixy::mint_tagged<::fixy::tags::source::Vendor, std::uint64_t>(1);
    id_v1.bios_revision = ::fixy::mint_tagged<::fixy::tags::source::Vendor, std::uint64_t>(42);

    cog::CogIdentity id_v2 = id_v1;
    id_v2.firmware_revision = ::fixy::mint_tagged<::fixy::tags::source::Vendor, std::uint64_t>(2);

    cog::GpuTargetCaps const caps = gpu_caps(90, 132);

    auto const mimic_v1 = mint_gpu(id_v1, caps);
    auto const mimic_v2 = mint_gpu(id_v2, caps);

    volatile std::uint64_t fed_v1 = mimic_v1.target_caps_class_hash();
    volatile std::uint64_t fed_v2 = mimic_v2.target_caps_class_hash();
    assert(fed_v1 == fed_v2);

    volatile std::uint64_t cog_v1 = mimic_v1.cog_kernel_cache_key();
    volatile std::uint64_t cog_v2 = mimic_v2.cog_kernel_cache_key();
    assert(cog_v1 != cog_v2);

    cog::CogIdentity id_bios_drift = id_v1;
    id_bios_drift.bios_revision = ::fixy::mint_tagged<::fixy::tags::source::Vendor, std::uint64_t>(43);

    auto const mimic_bios_drift = mint_gpu(id_bios_drift, mimic_v1.calibrated_caps().value());

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
    id.firmware_revision = ::fixy::mint_tagged<::fixy::tags::source::Vendor, std::uint64_t>(0xABCDULL);
    id.bios_revision = ::fixy::mint_tagged<::fixy::tags::source::Vendor, std::uint64_t>(0x1234ULL);

    cog::GpuTargetCaps const caps = gpu_caps(90, 132);

    cog::OpcodeLatencyTable<cog::CogKind::Gpu> tbl{};

    auto m = mimic::mint_cog_mimic<cog::CogKind::Gpu>(init_context(), id, caps, tbl);

    assert(&m.identity() == &id);
    assert(m.identity().kind == cog::CogKind::Gpu);

    volatile std::uint16_t round_trip_sm_version = m.calibrated_caps().value().sm_version.value();
    volatile std::uint16_t round_trip_sm_count = m.calibrated_caps().value().sm_count.value();
    assert(round_trip_sm_version == 90);
    assert(round_trip_sm_count == 132);

    assert(m.opcode_latency_table().empty());

    // The identity is bound, but an empty opcode table still counts as
    // uncalibrated.
    volatile bool uncal = m.is_uncalibrated();
    assert(uncal);

    // The per-Cog key needs an identity that holds a non-zero uuid, which
    // the mint has supplied.
    volatile std::uint64_t per_cog_key = m.cog_kernel_cache_key();
    volatile std::uint64_t federation = m.target_caps_class_hash();
    assert(per_cog_key != federation);  // fmix64(fed XOR content_hash) ≠ fed
    assert(per_cog_key != 0);
    assert(federation != 0);

    auto m_bg = mimic::mint_cog_mimic<cog::CogKind::Gpu>(background_context(), id, caps, tbl);
    assert(&m_bg.identity() == &id);
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
    cpu_id.firmware_revision = ::fixy::mint_tagged<::fixy::tags::source::Vendor, std::uint64_t>(0xC0DEULL);

    cog::CpuCoreTargetCaps cpu_caps{};
    cpu_caps.base_clock_mhz = ::fixy::mint_tagged<::fixy::tags::source::Vendor, std::uint32_t>(2500U);
    cpu_caps.l2_bytes = ::fixy::mint_tagged<::fixy::tags::source::Vendor, std::uint32_t>(1U * 1024 * 1024);

    auto const ctx = init_context();

    auto cpu_core_mimic = mimic::mint_cog_mimic<cog::CogKind::CpuCore>(
        ctx, cpu_id, cpu_caps, cog::OpcodeLatencyTable<cog::CogKind::CpuCore>{});
    assert(&cpu_core_mimic.identity() == &cpu_id);
    volatile std::uint64_t cpu_core_fed = cpu_core_mimic.target_caps_class_hash();
    assert(cpu_core_fed != 0);

    cog::CogIdentity sock_id{};
    sock_id.uuid = cog::Uuid{0x100ULL, 0x200ULL};
    sock_id.kind = cog::CogKind::CpuSocket;

    cog::CpuSocketTargetCaps sock_caps{};
    sock_caps.core_count = ::fixy::mint_tagged<::fixy::tags::source::Vendor, std::uint16_t>(96);
    sock_caps.l3_bytes =
        ::fixy::mint_tagged<::fixy::tags::source::Vendor, std::uint64_t>(std::uint64_t{384U} * 1024 * 1024);

    auto sock_mimic = mimic::mint_cog_mimic<cog::CogKind::CpuSocket>(
        ctx, sock_id, sock_caps, cog::OpcodeLatencyTable<cog::CogKind::CpuSocket>{});
    assert(&sock_mimic.identity() == &sock_id);
    volatile std::uint64_t sock_fed = sock_mimic.target_caps_class_hash();
    assert(sock_fed != 0);

    assert(cpu_core_fed != sock_fed);

    std::printf("  test_mint_cpu_paths:                  PASSED\n");
}

// The assignments are written by hand, so they are driven with values the
// compiler cannot fold as well.
static void test_assignment_runtime() {
    cog::CogIdentity const id_a = identity_of<cog::CogKind::Gpu>(40, 1);
    cog::CogIdentity const id_b = identity_of<cog::CogKind::Gpu>(41, 1);
    volatile std::uint16_t sm_version = 100;
    auto target = mint_gpu(id_a, gpu_caps(90, 132));
    auto const source = mint_gpu(id_b, gpu_caps(sm_version, 208));
    target = source;
    assert(&target.identity() == &id_b);
    assert(target.calibrated_caps().value().sm_version.value() == 100);
    assert(target.cog_kernel_cache_key() == source.cog_kernel_cache_key());

    auto moved_into = mint_gpu(id_a, gpu_caps(90, 132));
    moved_into = mint_gpu(id_b, gpu_caps(sm_version, 208));
    assert(&moved_into.identity() == &id_b);
    assert(moved_into.target_caps_class_hash() == source.target_caps_class_hash());

    std::printf("  test_assignment_runtime:              PASSED\n");
}

// The header asserts admission at compile time.  Reading the same
// constants at runtime catches a refactor that drifts those assertions
// out of agreement with how the concept actually instantiates.

static void test_ctx_fits_cog_mimic_concept_gate_runtime() {
    using InitCtx = ::fixy::ColdInitCtx;
    using TestCtx = ::fixy::TestRunnerCtx;

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
    test_fresh_mint_state_runtime();
    test_target_caps_class_hash_determinism();
    test_target_caps_class_hash_sm_version_discrimination();
    test_cog_kernel_cache_key_firmware_rotation();
    test_mint_cog_mimic_round_trip();
    test_mint_cpu_paths();
    test_assignment_runtime();
    test_ctx_fits_cog_mimic_concept_gate_runtime();
    test_trivially_destructible_carrier();
    std::printf("test_cog_mimic: all PASSED\n");
    return 0;
}
