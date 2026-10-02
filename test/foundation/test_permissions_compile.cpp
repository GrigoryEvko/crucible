// The permission headers keep their compile-time checks in their check
// files.  This file includes all of the permission headers together
// under the project's warning flags, so a new one gets an include here.
// Reaching main is itself the claim: the whole include set compiled
// clean.

#include <foundation/permissions/Permission.h>
#include <foundation/permissions/PermissionFork.h>
#include <foundation/permissions/PermSet.h>
#include <foundation/permissions/ReadView.h>

#include <cstdio>
#include <cstdlib>
#include <tuple>
#include <type_traits>
#include <utility>
#include "../test_assert.h"

namespace {

struct TestFailure {};
struct PlainTag {
    using permission_row = ::foundation::effects::Row<>;
};
// Declared and given no row, so the relation must refuse it.
struct UndeclaredTag {};

int total_passed = 0;
int total_failed = 0;

template <typename F>
void run_test(const char* name, F&& body) {
    std::fprintf(stderr, "  %s: ", name);
    try {
        body();
        ++total_passed;
        std::fprintf(stderr, "PASSED\n");
    } catch (TestFailure&) {
        ++total_failed;
        std::fprintf(stderr, "FAILED\n");
    }
}

}  // namespace

namespace permission_row_compile_tags {

struct Whole {};
struct IoChild {};
struct BlockChild {};

}  // namespace permission_row_compile_tags

// The rows are edges of the closed relation, declared before the first
// mint of the tags.
namespace foundation::permissions::permission_rows {

inline constexpr ::foundation::fail_closed::edge<
    permission_row_compile_tags::Whole,
    ::foundation::effects::Row<::foundation::effects::Effect::IO, ::foundation::effects::Effect::Block>>
    compile_whole{};
inline constexpr ::foundation::fail_closed::edge<permission_row_compile_tags::IoChild,
                                                 ::foundation::effects::Row<::foundation::effects::Effect::IO>>
    compile_io_child{};
inline constexpr ::foundation::fail_closed::edge<permission_row_compile_tags::BlockChild,
                                                 ::foundation::effects::Row<::foundation::effects::Effect::Block>>
    compile_block_child{};

}  // namespace foundation::permissions::permission_rows

namespace foundation::permissions {

template <>
struct can_split_into<permission_row_compile_tags::Whole, permission_row_compile_tags::IoChild,
                      permission_row_compile_tags::BlockChild> : std::true_type {};

template <>
struct can_split_into_pack<permission_row_compile_tags::Whole, permission_row_compile_tags::IoChild,
                           permission_row_compile_tags::BlockChild> : std::true_type {};

template <>
struct has_split_authoring_witness<permission_row_compile_tags::Whole, permission_row_compile_tags::IoChild,
                                   permission_row_compile_tags::BlockChild> : std::true_type {};

template <>
struct has_split_pack_authoring_witness<permission_row_compile_tags::Whole, permission_row_compile_tags::IoChild,
                                        permission_row_compile_tags::BlockChild> : std::true_type {};

}  // namespace foundation::permissions

namespace {

// Minting a root permission twice for one tag is allowed.  Linearity
// is a property of each token, which is move-only and consumed once,
// not a limit on how many tokens may exist.  The three groups below
// pin that reading: what the tag concept accepts, that a second root
// mint compiles, and that a token cannot be copied.
namespace per_token_linearity_pin {
struct EmptyTag {
    using permission_row = ::foundation::effects::Row<>;
};
struct NonEmptyTag {
    int payload = 0;
};
union UnionTag {
    int a;
};

static_assert(::foundation::permissions::PermissionTag<EmptyTag>, "A permission tag must be an empty class type.");
static_assert(!::foundation::permissions::PermissionTag<int>, "A permission tag must not be a primitive type.");
static_assert(!::foundation::permissions::PermissionTag<int*>, "A permission tag must not be a pointer type.");
static_assert(!::foundation::permissions::PermissionTag<NonEmptyTag>, "A permission tag must not carry data members.");
static_assert(!::foundation::permissions::PermissionTag<UnionTag>, "A permission tag must not be a union.");

// Two roots for one tag coexist as independent tokens, each consumed
// on its own.
[[maybe_unused]] constexpr auto reentrant_mint_witness_ = [] {
    auto a = ::foundation::permissions::mint_permission_root<EmptyTag>();
    auto b = ::foundation::permissions::mint_permission_root<EmptyTag>();
    (void)a;
    (void)b;
    return 0;
}();

static_assert(!std::is_copy_constructible_v<::foundation::permissions::Permission<EmptyTag>>,
              "A permission must not be copyable.  The absent copy constructor "
              "is what makes a token linear.");
static_assert(std::is_move_constructible_v<::foundation::permissions::Permission<EmptyTag>>,
              "A permission must be move-constructible, which is the only way "
              "ownership of a token transfers.");
}  // namespace per_token_linearity_pin

// The tag extractors give the region tag of a token through every
// reference and cv qualifier.  Each one refuses a type of the other
// kind, and a type that is no token, at the alias.
namespace tag_extractor_pin {
namespace fp = ::foundation::permissions;

struct RegionA {
    using permission_row = ::foundation::effects::Row<>;
};
struct RegionB {
    using permission_row = ::foundation::effects::Row<>;
};
struct PinBrand {};

using ExclusiveA = fp::Permission<RegionA, PinBrand>;
using ExclusiveB = fp::Permission<RegionB, PinBrand>;
using SharedA = fp::SharedPermission<RegionA, PinBrand>;

template <typename T>
concept has_permission_tag = requires { typename fp::permission_tag_t<T>; };
template <typename T>
concept has_shared_permission_tag = requires { typename fp::shared_permission_tag_t<T>; };

static_assert(std::is_same_v<fp::permission_tag_t<ExclusiveA>, RegionA>);
static_assert(std::is_same_v<fp::permission_tag_t<ExclusiveB>, RegionB>);
static_assert(std::is_same_v<fp::permission_tag_t<ExclusiveA&>, RegionA>);
static_assert(std::is_same_v<fp::permission_tag_t<ExclusiveA const&>, RegionA>);
static_assert(std::is_same_v<fp::permission_tag_t<ExclusiveA&&>, RegionA>);
static_assert(std::is_same_v<fp::permission_tag_t<ExclusiveA const volatile>, RegionA>);
static_assert(std::is_same_v<fp::shared_permission_tag_t<SharedA>, RegionA>);
static_assert(std::is_same_v<fp::shared_permission_tag_t<SharedA const&>, RegionA>);
static_assert(std::is_same_v<fp::shared_permission_tag_t<SharedA&&>, RegionA>);

static_assert(!has_permission_tag<SharedA>,
              "A shared permission is not an exclusive token, so it has no exclusive tag.");
static_assert(!has_permission_tag<RegionA>, "A region tag is not a token.");
static_assert(!has_permission_tag<int>, "A plain value is not a token.");
static_assert(!has_permission_tag<ExclusiveA*>, "A pointer to a token is not a token.");
static_assert(!has_shared_permission_tag<ExclusiveA>, "An exclusive token is not a shared permission.");
static_assert(!has_shared_permission_tag<int>, "A plain value is not a shared permission.");
}  // namespace tag_extractor_pin

void test_permission_compile() {}
void test_permission_fork_compile() {}
void test_permission_row_compile() {
    namespace eff = ::foundation::effects;
    namespace perm = ::foundation::permissions;

    using HotFgCtx = eff::ExecCtx<eff::ctx_cap::Fg, eff::Row<>>;
    using BgDrainCtx = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::Alloc>>;
    using BgCompileCtx = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::Alloc, eff::Effect::IO>>;
    using TestRunnerCtx =
        eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::Alloc, eff::Effect::IO, eff::Effect::Block>>;

    using HugePage = perm::tag::HugePageTag;
    using DiskSpilled = perm::tag::DiskSpilledRegionTag;
    using GpuMemory = perm::tag::GpuMemoryTag;
    using MmapRegion = perm::tag::MmapRegionTag;
    using NetworkBuffer = perm::tag::NetworkBufferTag;

    static_assert(perm::permission_row_empty(^^PlainTag));
    // The relation is closed: a tag with no declared row has none, is
    // not empty-rowed, and is admitted by no context.
    static_assert(!perm::has_permission_row(^^UndeclaredTag));
    static_assert(!perm::permission_row_empty(^^UndeclaredTag));
    static_assert(!perm::CtxAdmitsPermission<UndeclaredTag, TestRunnerCtx>);
    // A token over such a tag can be named, and its row hash folds no row
    // rather than stopping the build, so the hash of a type that is only
    // named needs no declared row.  The hash folds no brand, so the brand
    // here is any empty class.
    struct NamedOnlyBrand {};
    static_assert(::foundation::diag::row_hash_contribution_v<perm::Permission<UndeclaredTag, NamedOnlyBrand>>
                  != ::foundation::diag::row_hash_contribution_v<perm::Permission<HugePage, NamedOnlyBrand>>);
    static_assert(perm::CtxAdmitsPermission<HugePage, BgCompileCtx>);
    static_assert(!perm::CtxAdmitsPermission<HugePage, HotFgCtx>);
    static_assert(!perm::CtxAdmitsPermission<HugePage, BgDrainCtx>);
    static_assert(perm::CtxAdmitsPermission<DiskSpilled, TestRunnerCtx>);
    static_assert(!perm::CtxAdmitsPermission<DiskSpilled, BgCompileCtx>);
    static_assert(perm::CtxAdmitsPermission<GpuMemory, BgDrainCtx>);
    static_assert(!perm::CtxAdmitsPermission<GpuMemory, HotFgCtx>);
    static_assert(perm::CtxAdmitsPermission<MmapRegion, BgCompileCtx>);
    static_assert(!perm::CtxAdmitsPermission<MmapRegion, HotFgCtx>);
    static_assert(perm::CtxAdmitsPermission<NetworkBuffer, BgCompileCtx>);
    static_assert(!perm::CtxAdmitsPermission<NetworkBuffer, HotFgCtx>);

    // Each context is handed the capability it claims.
    BgCompileCtx bg_compile{::foundation::effects::testing::bg()};
    auto huge = perm::mint_permission_root<HugePage>(bg_compile);
    auto huge_shared = perm::mint_permission_share(bg_compile, std::move(huge));
    (void)huge_shared;

    auto huge_for_pool = perm::mint_permission_root<HugePage>(bg_compile);
    perm::SharedPermissionPool pool{std::move(huge_for_pool)};
    {
        auto guard = pool.lend(bg_compile);
        if (!guard) std::abort();
    }
    using PoolBrand = decltype(pool)::brand_type;
    auto value = perm::with_shared_read(bg_compile, pool,
                                        [](perm::SharedPermission<HugePage, PoolBrand>) noexcept { return 7; });
    if (!value || *value != 7) std::abort();

    TestRunnerCtx test_ctx{::foundation::effects::testing::test()};
    auto disk = perm::mint_permission_root<DiskSpilled>(test_ctx);
    auto handed = perm::permission_handoff(test_ctx, std::move(disk));
    perm::permission_drop(std::move(handed));

    using RowWhole = permission_row_compile_tags::Whole;
    using RowLeft = permission_row_compile_tags::IoChild;
    using RowRight = permission_row_compile_tags::BlockChild;

    auto whole = perm::mint_permission_root<RowWhole>(test_ctx);
    auto split = perm::mint_permission_split<RowLeft, RowRight>(test_ctx, std::move(whole));
    auto joined = perm::mint_permission_combine<RowWhole>(test_ctx, std::move(split.first), std::move(split.second));
    auto split_n = perm::mint_permission_split_n<RowLeft, RowRight>(test_ctx, std::move(joined));
    auto joined_n = perm::mint_permission_combine_n<RowWhole>(test_ctx, std::move(std::get<0>(split_n)),
                                                              std::move(std::get<1>(split_n)));
    perm::permission_drop(std::move(joined_n));
}
// The body names the fixture tags of detail::permset_smoke in
// PermSet.h, so the two using-directives bring those names into scope.
void test_perm_set_compile() {
    using namespace ::foundation::permissions;
    using namespace ::foundation::permissions::detail::permset_smoke;
    constexpr auto empty_size = EmptyPermSet::size;
    constexpr auto three_size = PermSet<A_tag, B_tag, C_tag>::size;
    static_assert(empty_size == 0);
    static_assert(three_size == 3);

    constexpr auto name = perm_set_name<PermSet<A_tag, B_tag>>();
    static_assert(!name.empty());

    static_assert(perm_set_equal(^^perm_set_canonicalize_t<PermSet<A_tag, B_tag>>,
                                 ^^perm_set_canonicalize_t<PermSet<B_tag, A_tag>>));
}
void test_read_view_compile() {}

}  // namespace

int main() {
    std::fprintf(stderr, "test_permissions_compile:\n");
    run_test("test_permission_compile", test_permission_compile);
    run_test("test_permission_fork_compile", test_permission_fork_compile);
    run_test("test_permission_row_compile", test_permission_row_compile);
    run_test("test_perm_set_compile", test_perm_set_compile);
    run_test("test_read_view_compile", test_read_view_compile);
    crucible::test::pass("\n{} passed, {} failed\n", total_passed, total_failed);
    if (total_failed > 0) return EXIT_FAILURE;
    crucible::test::pass("ALL PASSED\n");
    return EXIT_SUCCESS;
}
