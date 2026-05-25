// ═══════════════════════════════════════════════════════════════════
// test_permissions_compile — sentinel TU for permissions/* tree
//
// Same blind-spot rationale as test_algebra_compile / test_safety_
// compile (see feedback_header_only_static_assert_blind_spot memory).
// Forces every permissions/* header through the test target's full
// -Werror matrix.  Type-level checks live in this TU or in dedicated
// negative compile fixtures; reaching main proves the include set was
// processed clean.
//
// Coverage: 7 headers (Permission, PermissionFork, PermissionInherit,
// FairSharedPermissionPool, Permissions, PermSet, ReadView).  When a new permissions/* header
// ships, add its include below.
// ═══════════════════════════════════════════════════════════════════

#include <crucible/permissions/FairSharedPermissionPool.h>
#include <crucible/permissions/Permission.h>
#include <crucible/permissions/PermissionFork.h>
#include <crucible/permissions/PermissionInherit.h>
#include <crucible/permissions/Permissions.h>
#include <crucible/permissions/PermSet.h>
#include <crucible/permissions/ReadView.h>

#include <cstdio>
#include <cstdlib>
#include <tuple>
#include <type_traits>
#include <utility>

namespace {

struct TestFailure {};
struct InheritWorkerTag {};
struct InheritCoordTag {};
struct InheritMasterTag {};
struct InheritNonInheritingTag {};

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

namespace crucible::permissions {

template <>
struct survivor_registry<InheritWorkerTag> {
    using type = inheritance_list<InheritCoordTag>;
};

template <>
struct survivor_registry<InheritCoordTag> {
    using type = inheritance_list<InheritMasterTag>;
};

}  // namespace crucible::permissions

namespace permission_row_compile_tags {

struct Whole {};
struct IoChild {};
struct BlockChild {};

}  // namespace permission_row_compile_tags

namespace crucible::safety {

template <>
struct permission_row<permission_row_compile_tags::Whole> {
    using type = ::crucible::effects::Row<
        ::crucible::effects::Effect::IO,
        ::crucible::effects::Effect::Block>;
};

template <>
struct permission_row<permission_row_compile_tags::IoChild> {
    using type = ::crucible::effects::Row<::crucible::effects::Effect::IO>;
};

template <>
struct permission_row<permission_row_compile_tags::BlockChild> {
    using type = ::crucible::effects::Row<::crucible::effects::Effect::Block>;
};

template <>
struct splits_into<
    permission_row_compile_tags::Whole,
    permission_row_compile_tags::IoChild,
    permission_row_compile_tags::BlockChild> : std::true_type {};

template <>
struct splits_into_pack<
    permission_row_compile_tags::Whole,
    permission_row_compile_tags::IoChild,
    permission_row_compile_tags::BlockChild> : std::true_type {};

// fixy-M-29 authoring witnesses.
template <>
struct splits_into_authoring_witness<
    permission_row_compile_tags::Whole,
    permission_row_compile_tags::IoChild,
    permission_row_compile_tags::BlockChild> : std::true_type {};

template <>
struct splits_into_pack_authoring_witness<
    permission_row_compile_tags::Whole,
    permission_row_compile_tags::IoChild,
    permission_row_compile_tags::BlockChild> : std::true_type {};

}  // namespace crucible::safety

namespace {

// FIXY-FOUND-008 sentinel — pin the by-design reentrancy property of
// mint_permission_root<Tag>().  The audit ticket framed reentrancy as
// "defeating linearity"; the doc-comment at permissions/Permission.h
// ~L640 records the audit conclusion (linearity is per-instance move-
// only, NOT once-per-program cardinality).  Pin the three structural
// witnesses here so a regression to the misframed premise reds this TU:
//   (1) PermissionTag concept rejects non-empty / non-class shapes.
//   (2) Reentrant root mint compiles for empty-row Tags (by design).
//   (3) Permission<Tag> is move-only (deleted copy) — the actual
//       linearity carrier.
namespace fixy_found_008_pin {
struct EmptyTag {};
struct NonEmptyTag { int payload = 0; };
union UnionTag { int a; };

static_assert(::crucible::safety::PermissionTag<EmptyTag>,
    "FIXY-FOUND-008: PermissionTag must accept empty class types.");
static_assert(!::crucible::safety::PermissionTag<int>,
    "FIXY-FOUND-008: PermissionTag must reject primitives.");
static_assert(!::crucible::safety::PermissionTag<int*>,
    "FIXY-FOUND-008: PermissionTag must reject pointers.");
static_assert(!::crucible::safety::PermissionTag<NonEmptyTag>,
    "FIXY-FOUND-008: PermissionTag must reject non-empty classes.");
static_assert(!::crucible::safety::PermissionTag<UnionTag>,
    "FIXY-FOUND-008: PermissionTag must reject unions.");

// Reentrant mint is by-design: two roots for the same Tag coexist as
// independent move-only tokens.  Each is independently consumable.
[[maybe_unused]] constexpr auto reentrant_mint_witness_ = [] {
    auto a = ::crucible::safety::mint_permission_root<EmptyTag>();
    auto b = ::crucible::safety::mint_permission_root<EmptyTag>();
    (void)a; (void)b;
    return 0;
}();

static_assert(!std::is_copy_constructible_v<
                  ::crucible::safety::Permission<EmptyTag>>,
    "FIXY-FOUND-008: Permission<Tag> must be non-copyable; copy ctor is "
    "the linearity carrier.  Reentrant mint produces independent move-"
    "only tokens, not aliasable copies.");
static_assert(std::is_move_constructible_v<
                  ::crucible::safety::Permission<EmptyTag>>,
    "FIXY-FOUND-008: Permission<Tag> must be move-constructible (the "
    "only legitimate ownership-transfer mechanism).");
}  // namespace fixy_found_008_pin

void test_permission_compile()      {}
void test_permission_fork_compile() {}
void test_permission_row_compile() {
    namespace eff = ::crucible::effects;
    namespace perm = ::crucible::permissions;
    namespace saf = ::crucible::safety;

    using HugePage = perm::tag::HugePageTag;
    using DiskSpilled = perm::tag::DiskSpilledRegionTag;
    using GpuMemory = perm::tag::GpuMemoryTag;
    using MmapRegion = perm::tag::MmapRegionTag;
    using NetworkBuffer = perm::tag::NetworkBufferTag;

    static_assert(saf::permission_row_empty_v<InheritWorkerTag>);
    static_assert(saf::CtxAdmitsPermission<HugePage, eff::BgCompileCtx>);
    static_assert(!saf::CtxAdmitsPermission<HugePage, eff::HotFgCtx>);
    static_assert(!saf::CtxAdmitsPermission<HugePage, eff::BgDrainCtx>);
    static_assert(saf::CtxAdmitsPermission<DiskSpilled, eff::TestRunnerCtx>);
    static_assert(!saf::CtxAdmitsPermission<DiskSpilled, eff::BgCompileCtx>);
    static_assert(saf::CtxAdmitsPermission<GpuMemory, eff::BgDrainCtx>);
    static_assert(!saf::CtxAdmitsPermission<GpuMemory, eff::HotFgCtx>);
    static_assert(saf::CtxAdmitsPermission<MmapRegion, eff::BgCompileCtx>);
    static_assert(!saf::CtxAdmitsPermission<MmapRegion, eff::HotFgCtx>);
    static_assert(saf::CtxAdmitsPermission<NetworkBuffer, eff::BgCompileCtx>);
    static_assert(!saf::CtxAdmitsPermission<NetworkBuffer, eff::HotFgCtx>);

    eff::BgCompileCtx bg_compile{};
    auto huge = saf::mint_permission_root<HugePage>(bg_compile);
    auto huge_shared = saf::mint_permission_share(bg_compile, std::move(huge));
    (void)huge_shared;

    auto huge_for_pool = saf::mint_permission_root<HugePage>(bg_compile);
    saf::SharedPermissionPool<HugePage> pool{std::move(huge_for_pool)};
    {
        auto guard = pool.lend(bg_compile);
        if (!guard) std::abort();
    }
    auto value = saf::with_shared_read(
        bg_compile,
        pool,
        [](saf::SharedPermission<HugePage>) noexcept { return 7; });
    if (!value || *value != 7) std::abort();

    auto huge_for_fair = saf::mint_permission_root<HugePage>(bg_compile);
    saf::FairSharedPermissionPool<HugePage> fair{std::move(huge_for_fair)};
    bool ran = saf::with_shared_read(
        bg_compile,
        fair,
        [](saf::SharedPermission<HugePage>) noexcept {});
    if (!ran) std::abort();

    eff::TestRunnerCtx test_ctx{};
    auto disk = saf::mint_permission_root<DiskSpilled>(test_ctx);
    auto handed = saf::permission_handoff(test_ctx, std::move(disk));
    saf::permission_drop(std::move(handed));

    using RowWhole = permission_row_compile_tags::Whole;
    using RowLeft = permission_row_compile_tags::IoChild;
    using RowRight = permission_row_compile_tags::BlockChild;

    auto whole = saf::mint_permission_root<RowWhole>(test_ctx);
    auto split = saf::mint_permission_split<RowLeft, RowRight>(
        test_ctx, std::move(whole));
    auto joined = saf::mint_permission_combine<RowWhole>(
        test_ctx, std::move(split.first), std::move(split.second));
    auto split_n = saf::mint_permission_split_n<RowLeft, RowRight>(
        test_ctx, std::move(joined));
    auto joined_n = saf::mint_permission_combine_n<RowWhole>(
        test_ctx, std::move(std::get<0>(split_n)), std::move(std::get<1>(split_n)));
    saf::permission_drop(std::move(joined_n));
}
void test_mint_permission_inherit_compile() {
    namespace pi = ::crucible::permissions;
    using WorkerSurvivors = pi::survivors_t<InheritWorkerTag>;
    static_assert(pi::inheritance_list_contains_v<
        WorkerSurvivors, InheritCoordTag>);
    static_assert(!pi::inheritance_list_contains_v<
        WorkerSurvivors, InheritNonInheritingTag>);
    static_assert(pi::inherits_from_v<InheritWorkerTag, InheritCoordTag>);
    static_assert(!pi::inherits_from_v<
        InheritWorkerTag, InheritNonInheritingTag>);

    // H-25: the public `mint_permission_inherit<...>()` factory now
    // requires a `crash_witness_key` parameter — a passkey that ONLY
    // `bridges::wrap_crash_return` can mint (private ctor, friend-gated).
    // So this TU cannot CALL the factory directly.  But we can still
    // assert its RETURN TYPE in an unevaluated context via `declval`,
    // which materializes a hypothetical key without constructing one.
    // The neg-compile fixtures (neg_permission_inherit_no_witness*) prove
    // that ACTUAL invocation without a witness is rejected; this TU
    // proves the type-level survivor-tuple shape is correct.
    using ExplicitTuple = decltype(
        pi::mint_permission_inherit<InheritWorkerTag, InheritCoordTag>(
            std::declval<pi::crash_witness_key>()));
    using RegistryTuple = decltype(
        pi::mint_permission_inherit<InheritWorkerTag>(
            std::declval<pi::crash_witness_key>()));
    static_assert(std::is_same_v<
        ExplicitTuple,
        std::tuple<::crucible::safety::Permission<InheritCoordTag>>>);
    static_assert(std::is_same_v<ExplicitTuple, RegistryTuple>);

    using ChainedTuple = decltype(
        pi::mint_permission_inherit<InheritCoordTag>(
            std::declval<pi::crash_witness_key>()));
    static_assert(std::is_same_v<
        ChainedTuple,
        std::tuple<::crucible::safety::Permission<InheritMasterTag>>>);

    // fixy-A1-029: the §XXI signature-clarity refactor exposes
    // `mint_permission_inherit_t<DeadTag, SurvivorTags...>` as the
    // public-API name for the survivor-tuple type.  Pin the alias to
    // each `decltype(...)` form so a future refactor that breaks the
    // identity (e.g. accidentally promoting `survivors_t<DeadTag>` to
    // a non-inheritance_list shape) reddens here too.
    static_assert(std::is_same_v<
        pi::mint_permission_inherit_t<InheritWorkerTag, InheritCoordTag>,
        ExplicitTuple>);
    static_assert(std::is_same_v<
        pi::mint_permission_inherit_t<InheritWorkerTag>,
        RegistryTuple>);
    static_assert(std::is_same_v<
        pi::mint_permission_inherit_t<InheritCoordTag>,
        ChainedTuple>);
    static_assert(std::is_same_v<
        pi::mint_permission_inherit_t<InheritWorkerTag, InheritCoordTag>,
        std::tuple<::crucible::safety::Permission<InheritCoordTag>>>);
}
void test_permissions_umbrella() {}
void test_perm_set_compile()     {}
void test_read_view_compile()    {}

}  // namespace

int main() {
    std::fprintf(stderr, "test_permissions_compile:\n");
    run_test("test_permission_compile",      test_permission_compile);
    run_test("test_permission_fork_compile", test_permission_fork_compile);
    run_test("test_permission_row_compile",  test_permission_row_compile);
    run_test("test_mint_permission_inherit_compile",
        test_mint_permission_inherit_compile);
    run_test("test_permissions_umbrella",    test_permissions_umbrella);
    run_test("test_perm_set_compile",        test_perm_set_compile);
    run_test("test_read_view_compile",       test_read_view_compile);
    std::fprintf(stderr, "\n%d passed, %d failed\n", total_passed, total_failed);
    if (total_failed > 0) return EXIT_FAILURE;
    std::fprintf(stderr, "ALL PASSED\n");
    return EXIT_SUCCESS;
}
