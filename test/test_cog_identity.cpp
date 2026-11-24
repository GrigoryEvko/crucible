// The include below is half the point of this file: a header's
// static_asserts are evaluated under the project warning and standard
// flags only when a translation unit in the build graph pulls the
// header in.  The other half is the runtime body, which drives the
// enumerator names and content_hash with non-constant arguments so they
// are not answered entirely by constant folding.

#include <crucible/cog/CogIdentity.h>
#include <crucible/topology/TopologyGraph.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>
#include <foundation/reflect/EnumName.h>

#include <cstdio>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

namespace cog = crucible::cog;

// Each enumerator's name comes back through a volatile, so the lookup runs
// at run time as well as in the constant folder.
template <typename E>
static void check_every_name_at_run_time() {
    ::fixy::for_each_enumerator_text<E>([](E value, ::fixy::TextView identifier) {
        volatile E runtime_value = value;
        const ::fixy::TextView name = ::fixy::enum_name_text(static_cast<E>(runtime_value));
        CRUCIBLE_TEST_ASSERT(kTest, name == identifier);
        CRUCIBLE_TEST_ASSERT(kTest, name != ::fixy::unknown_enum_text<E>);
    });
}

static void test_names_at_run_time() {
    check_every_name_at_run_time<cog::CogLevel>();
    check_every_name_at_run_time<cog::CogKind>();
    check_every_name_at_run_time<cog::CogFamily>();
    ::fixy::check::pass(kTest, "  test_names_at_run_time:              PASSED\n");
}

static void test_uuid_layout() {
    static_assert(sizeof(cog::Uuid) == 16);
    static_assert(std::is_trivially_copyable_v<cog::Uuid>);

    cog::Uuid zero{};
    cog::Uuid same_zero{0, 0};
    cog::Uuid distinct{0xDEADBEEFULL, 0xCAFEBABEULL};

    CRUCIBLE_TEST_ASSERT(kTest, zero.is_zero());
    CRUCIBLE_TEST_ASSERT(kTest, same_zero.is_zero());
    CRUCIBLE_TEST_ASSERT(kTest, zero == same_zero);
    CRUCIBLE_TEST_ASSERT(kTest, !distinct.is_zero());
    CRUCIBLE_TEST_ASSERT(kTest, zero != distinct);

    // Ordering is lexicographic on (hi, lo).  Checking it at runtime
    // keeps the claim independent of the in-header static_asserts.
    cog::Uuid lo_a{0, 1};
    cog::Uuid lo_b{0, 2};
    CRUCIBLE_TEST_ASSERT(kTest, lo_a < lo_b);

    cog::Uuid hi_a{1, 0};
    cog::Uuid hi_b{2, 0};
    CRUCIBLE_TEST_ASSERT(kTest, hi_a < hi_b);

    // A maximal lo cannot outrank a larger hi.
    cog::Uuid mixed_a{1, 0xFFFFFFFFFFFFFFFFULL};
    cog::Uuid mixed_b{2, 0};
    CRUCIBLE_TEST_ASSERT(kTest, mixed_a < mixed_b);

    ::fixy::check::pass(kTest, "  test_uuid_layout:                    PASSED\n");
}

static void test_cog_identity_runtime() {
    cog::CogIdentity gpu{};
    gpu.uuid = cog::Uuid{0xDEADBEEFULL, 0xCAFEBABEULL};
    gpu.level = cog::CogLevel::L0_Atomic;
    gpu.kind = cog::CogKind::Gpu;
    gpu.firmware_revision = ::fixy::mint_tagged<::fixy::tags::source::Vendor, std::uint64_t>(0x12345678ULL);
    gpu.bios_revision = ::fixy::mint_tagged<::fixy::tags::source::Vendor, std::uint64_t>(0xABCDEF01ULL);

    cog::CogIdentity gpu_copy = gpu;

    volatile std::uint64_t h1 = cog::content_hash(gpu);
    volatile std::uint64_t h2 = cog::content_hash(gpu_copy);
    CRUCIBLE_TEST_ASSERT(kTest, h1 == h2);

    cog::CogIdentity gpu_new_fw = gpu;
    gpu_new_fw.firmware_revision =
        ::fixy::mint_tagged<::fixy::tags::source::Vendor, std::uint64_t>(0xFFFFFFFFFFFFFFFFULL);
    volatile std::uint64_t h3 = cog::content_hash(gpu_new_fw);
    CRUCIBLE_TEST_ASSERT(kTest, h1 != h3);

    cog::CogIdentity gpu_new_bios = gpu;
    gpu_new_bios.bios_revision =
        ::fixy::mint_tagged<::fixy::tags::source::Vendor, std::uint64_t>(0xFFFFFFFFFFFFFFFFULL);
    volatile std::uint64_t h4 = cog::content_hash(gpu_new_bios);
    CRUCIBLE_TEST_ASSERT(kTest, h1 != h4);
    CRUCIBLE_TEST_ASSERT(kTest, h3 != h4);

    cog::CogIdentity gpu_other = gpu;
    gpu_other.uuid = cog::Uuid{0xFFFFFFFFFFFFFFFFULL, 0x1234567890ABCDEFULL};
    volatile std::uint64_t h5 = cog::content_hash(gpu_other);
    CRUCIBLE_TEST_ASSERT(kTest, h1 != h5);

    ::fixy::check::pass(kTest, "  test_cog_identity_runtime:           PASSED\n");
}

static void test_is_compute_kind_partitioning() {
    static_assert(cog::IsComputeKind<cog::CogKind::Gpu>);
    static_assert(cog::IsComputeKind<cog::CogKind::CpuCore>);
    static_assert(cog::IsComputeKind<cog::CogKind::GpuPackage>);
    static_assert(cog::IsComputeKind<cog::CogKind::CpuSocket>);
    static_assert(!cog::IsComputeKind<cog::CogKind::NicPort>);
    static_assert(!cog::IsComputeKind<cog::CogKind::NvSwitch>);
    static_assert(!cog::IsComputeKind<cog::CogKind::Datacenter>);

    // The constrained lambda proves the concept gates instantiation,
    // not merely that it evaluates to the right bool.
    auto count = []<cog::CogKind K>()
        requires cog::IsComputeKind<K>
    { return std::size_t{1}; };

    volatile std::size_t total =
        count.template operator()<cog::CogKind::Gpu>() + count.template operator()<cog::CogKind::CpuCore>()
        + count.template operator()<cog::CogKind::GpuPackage>() + count.template operator()<cog::CogKind::CpuSocket>();
    CRUCIBLE_TEST_ASSERT(kTest, total == 4);

    ::fixy::check::pass(kTest, "  test_is_compute_kind_partitioning:   PASSED\n");
}

static void test_cog_identity_topology_links() {
    cog::CogIdentity gpu0{};
    gpu0.uuid = cog::Uuid{1, 0};
    gpu0.level = cog::CogLevel::L0_Atomic;
    gpu0.kind = cog::CogKind::Gpu;

    cog::CogIdentity gpu1{};
    gpu1.uuid = cog::Uuid{2, 0};
    gpu1.level = cog::CogLevel::L0_Atomic;
    gpu1.kind = cog::CogKind::Gpu;

    const ::foundation::core::FixedArray<cog::CogIdentity, 2> gpu_children{gpu0, gpu1};

    cog::CogIdentity package{};
    package.uuid = cog::Uuid{0, 1};
    package.level = cog::CogLevel::L1_Component;
    package.kind = cog::CogKind::GpuPackage;
    package.children = gpu_children.view();

    // The volatile barrier stops the walk below from being folded out.
    volatile std::size_t observed_children = package.children.size();
    CRUCIBLE_TEST_ASSERT(kTest, observed_children == 2);

    std::size_t walked = 0;
    for (cog::CogIdentity const& child : package.children) {
        CRUCIBLE_TEST_ASSERT(kTest, child.level == cog::CogLevel::L0_Atomic);
        CRUCIBLE_TEST_ASSERT(kTest, child.kind == cog::CogKind::Gpu);
        CRUCIBLE_TEST_ASSERT(kTest, !child.uuid.is_zero());
        ++walked;
    }
    CRUCIBLE_TEST_ASSERT(kTest, walked == 2);

    CRUCIBLE_TEST_ASSERT(kTest, package.parent.is_none());

    CRUCIBLE_TEST_ASSERT(kTest, package.neighbors_l2.size() == 0);
    CRUCIBLE_TEST_ASSERT(kTest, package.neighbors_l3.size() == 0);

    // A child names its package as its parent.
    cog::CogIdentity child = gpu0;
    child.parent = ::fixy::some(::fixy::Ref<const cog::CogIdentity>{::fixy::erase_brand(::fixy::to(package))});
    CRUCIBLE_TEST_ASSERT(kTest, child.parent.match(
                                    [&package](::fixy::Ref<const cog::CogIdentity> parent) noexcept {
                                        return ::fixy::same_object(parent, ::fixy::to(package));
                                    },
                                    []() noexcept { return false; }));

    // The neighbor views hold the edges of the topology graph.
    const ::foundation::core::FixedArray<crucible::topology::TopologyEdge, 2> edges{};
    package.neighbors_l2 = edges.view();
    CRUCIBLE_TEST_ASSERT(kTest, package.neighbors_l2.size() == 2);
    CRUCIBLE_TEST_ASSERT(kTest, package.neighbors_l3.size() == 0);

    ::fixy::check::pass(kTest, "  test_cog_identity_topology_links:    PASSED\n");
}

int main() {
    ::fixy::report(::fixy::Sink::Out, "test_cog_identity: 5 groups\n");
    test_names_at_run_time();
    test_uuid_layout();
    test_cog_identity_runtime();
    test_is_compute_kind_partitioning();
    test_cog_identity_topology_links();
    ::fixy::check::pass(kTest, "test_cog_identity: 5 groups, all passed\n");
    return 0;
}
