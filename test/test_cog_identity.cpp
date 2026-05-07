// Sentinel TU for include/crucible/cog/CogIdentity.h.
//
// Per feedback_header_only_static_assert_blind_spot.md: header-only
// static_asserts inside CogIdentity.h are only evaluated under the
// project's full warning + standard flags when SOMEONE includes the
// header from a TU that lands in the build graph.  This sentinel
// makes the inclusion explicit so the cog_identity_self_test block
// (FOUND-I04 frozen-position pin lines + reflection-driven name
// coverage + content_hash determinism) is exercised by every default
// build.
//
// The runtime portion exercises the constexpr name accessors and the
// `content_hash` Family-A mixer with non-constant arguments (per
// feedback_algebra_runtime_smoke_test_discipline) and confirms the
// reflected enumerator-count cardinalities match the manually pinned
// counts.  No data flows out of this TU — its purpose is purely to
// pull the header into the warning-flag-controlled compilation unit
// graph and exercise the runtime-side accessors.
//
// GAPS-185.

#include <crucible/cog/CogIdentity.h>

#include "test_assert.h"

#include <cstdio>
#include <string_view>

namespace cog = crucible::cog;

// ── Reflection-driven name coverage at runtime ──────────────────────

static void test_cog_level_name_coverage() {
    constexpr cog::CogLevel levels[] = {
        cog::CogLevel::L0_Atomic,
        cog::CogLevel::L1_Component,
        cog::CogLevel::L2_Board,
        cog::CogLevel::L3_Chassis,
        cog::CogLevel::L4_Rack,
        cog::CogLevel::L5_Row,
        cog::CogLevel::L6_Hall,
        cog::CogLevel::L7_Datacenter,
    };

    static_assert(sizeof(levels) / sizeof(levels[0]) == cog::cog_level_count,
        "Manual levels[] table diverged from cog_level_count — add the "
        "new atom to this table when extending the hierarchy.");

    for (cog::CogLevel L : levels) {
        // Volatile barrier prevents the optimizer from constant-folding
        // through the static_asserts in-header.
        volatile auto vL = L;
        std::string_view name = cog::cog_level_name(
            static_cast<cog::CogLevel>(vL));
        assert(!name.empty());
        assert(name != std::string_view{"<unknown CogLevel>"});
    }
    std::printf("  test_cog_level_name_coverage:        PASSED\n");
}

static void test_cog_kind_name_coverage() {
    constexpr cog::CogKind kinds[] = {
        cog::CogKind::Gpu,
        cog::CogKind::NicPort,
        cog::CogKind::CpuCore,
        cog::CogKind::DramChannel,
        cog::CogKind::NvmeNamespace,
        cog::CogKind::NvSwitch,
        cog::CogKind::OpticalTransceiver,
        cog::CogKind::PsuRail,
        cog::CogKind::PcieLaneGroup,
        cog::CogKind::BmcSensor,
        cog::CogKind::GpuPackage,
        cog::CogKind::CpuSocket,
        cog::CogKind::NicCard,
        cog::CogKind::NvmeDrive,
        cog::CogKind::RackPsu,
        cog::CogKind::PcieRoot,
        cog::CogKind::Server,
        cog::CogKind::Rack,
        cog::CogKind::Row,
        cog::CogKind::Hall,
        cog::CogKind::Datacenter,
    };

    static_assert(sizeof(kinds) / sizeof(kinds[0]) == cog::cog_kind_count,
        "Manual kinds[] table diverged from cog_kind_count — add the "
        "new atom to this table when extending the catalog.");

    for (cog::CogKind K : kinds) {
        volatile auto vK = K;
        std::string_view name = cog::cog_kind_name(
            static_cast<cog::CogKind>(vK));
        assert(!name.empty());
        assert(name != std::string_view{"<unknown CogKind>"});
    }
    std::printf("  test_cog_kind_name_coverage:         PASSED\n");
}

// ── Uuid layout + equality smoke ────────────────────────────────────

static void test_uuid_layout() {
    static_assert(sizeof(cog::Uuid) == 16);
    static_assert(std::is_trivially_copyable_v<cog::Uuid>);

    cog::Uuid zero{};
    cog::Uuid same_zero{0, 0};
    cog::Uuid distinct{0xDEADBEEFULL, 0xCAFEBABEULL};

    assert(zero.is_zero());
    assert(same_zero.is_zero());
    assert(zero == same_zero);
    assert(!distinct.is_zero());
    assert(zero != distinct);

    // Spaceship ordering: lexicographic on (hi, lo) under default
    // operator<=> — pin via runtime check that's independent of the
    // in-header static_asserts.
    cog::Uuid lo_a{0, 1};
    cog::Uuid lo_b{0, 2};
    assert(lo_a < lo_b);

    cog::Uuid hi_a{1, 0};
    cog::Uuid hi_b{2, 0};
    assert(hi_a < hi_b);

    // hi dominates lo in ordering.
    cog::Uuid mixed_a{1, 0xFFFFFFFFFFFFFFFFULL};
    cog::Uuid mixed_b{2, 0};
    assert(mixed_a < mixed_b);

    std::printf("  test_uuid_layout:                    PASSED\n");
}

// ── CogIdentity layout + content_hash runtime determinism ───────────

static void test_cog_identity_runtime() {
    cog::CogIdentity gpu{};
    gpu.uuid              = cog::Uuid{0xDEADBEEFULL, 0xCAFEBABEULL};
    gpu.level             = cog::CogLevel::L0_Atomic;
    gpu.kind              = cog::CogKind::Gpu;
    gpu.firmware_revision = crucible::safety::Tagged<std::uint64_t,
        crucible::safety::source::Vendor>{0x12345678ULL};
    gpu.bios_revision     = crucible::safety::Tagged<std::uint64_t,
        crucible::safety::source::Vendor>{0xABCDEF01ULL};

    cog::CogIdentity gpu_copy = gpu;

    // Determinism: same triple → same hash.
    volatile std::uint64_t h1 = cog::content_hash(gpu);
    volatile std::uint64_t h2 = cog::content_hash(gpu_copy);
    assert(h1 == h2);

    // Discrimination: firmware drift changes the hash.
    cog::CogIdentity gpu_new_fw = gpu;
    gpu_new_fw.firmware_revision =
        crucible::safety::Tagged<std::uint64_t,
            crucible::safety::source::Vendor>{0xFFFFFFFFFFFFFFFFULL};
    volatile std::uint64_t h3 = cog::content_hash(gpu_new_fw);
    assert(h1 != h3);

    // Discrimination: bios drift changes the hash.
    cog::CogIdentity gpu_new_bios = gpu;
    gpu_new_bios.bios_revision =
        crucible::safety::Tagged<std::uint64_t,
            crucible::safety::source::Vendor>{0xFFFFFFFFFFFFFFFFULL};
    volatile std::uint64_t h4 = cog::content_hash(gpu_new_bios);
    assert(h1 != h4);
    assert(h3 != h4);

    // Discrimination: uuid drift changes the hash even with identical
    // firmware/bios.
    cog::CogIdentity gpu_other = gpu;
    gpu_other.uuid = cog::Uuid{0xFFFFFFFFFFFFFFFFULL, 0x1234567890ABCDEFULL};
    volatile std::uint64_t h5 = cog::content_hash(gpu_other);
    assert(h1 != h5);

    std::printf("  test_cog_identity_runtime:           PASSED\n");
}

// ── Compute-kind concept partitioning ───────────────────────────────

static void test_is_compute_kind_partitioning() {
    // Compile-time partition (matches the in-header static_asserts).
    static_assert( cog::IsComputeKind<cog::CogKind::Gpu>);
    static_assert( cog::IsComputeKind<cog::CogKind::CpuCore>);
    static_assert( cog::IsComputeKind<cog::CogKind::GpuPackage>);
    static_assert( cog::IsComputeKind<cog::CogKind::CpuSocket>);
    static_assert(!cog::IsComputeKind<cog::CogKind::NicPort>);
    static_assert(!cog::IsComputeKind<cog::CogKind::NvSwitch>);
    static_assert(!cog::IsComputeKind<cog::CogKind::Datacenter>);

    // Runtime confirmation that template-parameterized concept gates
    // function: a function templated on IsComputeKind returns 1 for
    // Gpu and the static_assert(!IsComputeKind<...>) prevents the same
    // function from instantiating with NicPort.
    auto count = []<cog::CogKind K>() requires cog::IsComputeKind<K> {
        return std::size_t{1};
    };

    volatile std::size_t total =
        count.template operator()<cog::CogKind::Gpu>()
      + count.template operator()<cog::CogKind::CpuCore>()
      + count.template operator()<cog::CogKind::GpuPackage>()
      + count.template operator()<cog::CogKind::CpuSocket>();
    assert(total == 4);

    std::printf("  test_is_compute_kind_partitioning:   PASSED\n");
}

// ── Topology graph smoke (parent + children fields) ─────────────────

static void test_cog_identity_topology_links() {
    // Mini hierarchy: one L1 GpuPackage with two L0 Gpu children.
    cog::CogIdentity gpu0{};
    gpu0.uuid  = cog::Uuid{1, 0};
    gpu0.level = cog::CogLevel::L0_Atomic;
    gpu0.kind  = cog::CogKind::Gpu;

    cog::CogIdentity gpu1{};
    gpu1.uuid  = cog::Uuid{2, 0};
    gpu1.level = cog::CogLevel::L0_Atomic;
    gpu1.kind  = cog::CogKind::Gpu;

    const cog::CogIdentity gpu_children[2] = { gpu0, gpu1 };

    cog::CogIdentity package{};
    package.uuid     = cog::Uuid{0, 1};
    package.level    = cog::CogLevel::L1_Component;
    package.kind     = cog::CogKind::GpuPackage;
    package.children = std::span<const cog::CogIdentity>(gpu_children, 2);

    // Volatile barrier so the loop is not constant-folded out.
    volatile std::size_t observed_children = package.children.size();
    assert(observed_children == 2);

    std::size_t walked = 0;
    for (cog::CogIdentity const& child : package.children) {
        assert(child.level == cog::CogLevel::L0_Atomic);
        assert(child.kind  == cog::CogKind::Gpu);
        assert(!child.uuid.is_zero());
        ++walked;
    }
    assert(walked == 2);

    // Parent pointer is nullable — package has no parent in this
    // fixture (it's the local root).
    assert(package.parent == nullptr);

    // Neighbor edges default to empty spans.
    assert(package.neighbors_l2.empty());
    assert(package.neighbors_l3.empty());

    std::printf("  test_cog_identity_topology_links:    PASSED\n");
}

int main() {
    std::printf("test_cog_identity: 6 groups\n");
    test_cog_level_name_coverage();
    test_cog_kind_name_coverage();
    test_uuid_layout();
    test_cog_identity_runtime();
    test_is_compute_kind_partitioning();
    test_cog_identity_topology_links();
    std::printf("test_cog_identity: 6 groups, all passed\n");
    return 0;
}
