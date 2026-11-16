// The three axes as one surface: a read that a brand, a share and an
// effect row all agree on.
//
// The static cells stand on what the type system refuses.  The runtime
// cells stand on what only a running program shows: that a read sees
// the bytes the region holds, that an upgrade waits for every share,
// and that two readers hold one region at once.

#include <fixy/SharedRegion.h>

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>
#include <foundation/permissions/Permission.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace eff = ::foundation::effects;
namespace perm = ::foundation::permissions;

namespace {

using ::fixy::TestRunnerCtx;
using ::fixy::check::pass;

// A lend and an upgrade of a pool read the schedule, so the context holds
// Nondet.
using BgCtx = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::Nondet>>;

struct Cache {
    using permission_row = eff::Row<>;
};
struct Spilled {
    using permission_row = eff::Row<eff::Effect::IO>;
};

// The question each cell below asks of the mint, written once.
template <typename Ctx, typename Guard, typename Region>
concept CanRead = requires(Ctx const& ctx, Guard const& guard, Region const& region) {
    ::fixy::mint_shared_read(ctx, guard, region);
};

void test_a_read_sees_the_region(TestRunnerCtx const& test) {
    static std::uint64_t storage[6] = {10, 11, 12, 13, 14, 15};
    BgCtx ctx{eff::testing::bg()};

    auto region = ::fixy::mint_owned_region(storage, std::size_t{6}, perm::mint_permission_root<Cache>());
    ::fixy::SharedRegion shared{std::move(region)};
    CRUCIBLE_TEST_ASSERT(test, shared.size() == 6);
    CRUCIBLE_TEST_ASSERT(test, !shared.empty());

    auto guard = shared.lend(ctx).expect("an idle region lends a share");

    auto read = ::fixy::mint_shared_read(ctx, guard, shared);
    CRUCIBLE_TEST_ASSERT(test, read.size() == 6);
    CRUCIBLE_TEST_ASSERT(test, read[0] == 10);
    CRUCIBLE_TEST_ASSERT(test, read[5] == 15);

    std::uint64_t sum = 0;
    for (std::uint64_t value : read)
        sum += value;
    CRUCIBLE_TEST_ASSERT(test, sum == 75);

    // The subscript checks its index in each build, the release build too,
    // so a read past the end ends the process.  The read runs in a child
    // through expect_dies of the Check family.
    ::fixy::check::expect_dies(
        test, [&read] noexcept { (void)read[read.size()]; },
        ::fixy::TextView{"foundation: contract violation: index < size()"});

    // The read names the instance it was taken against, and the share
    // it stands on is the guard's.
    static_assert(::foundation::brand::SameBrand<decltype(read), decltype(guard)>);
}

void test_an_upgrade_waits_for_every_share(TestRunnerCtx const& test) {
    static std::uint64_t storage[4] = {1, 2, 3, 4};
    BgCtx ctx{eff::testing::bg()};

    auto region = ::fixy::mint_owned_region(storage, std::size_t{4}, perm::mint_permission_root<Cache>());
    ::fixy::SharedRegion shared{std::move(region)};

    {
        auto guard = shared.lend(ctx);
        CRUCIBLE_TEST_ASSERT(test, guard.is_some());
        CRUCIBLE_TEST_ASSERT(test, shared.outstanding() == 1);
        CRUCIBLE_TEST_ASSERT(test, !shared.try_upgrade(ctx).has_value());
    }

    CRUCIBLE_TEST_ASSERT(test, shared.outstanding() == 0);
    auto exclusive = shared.try_upgrade(ctx);
    CRUCIBLE_TEST_ASSERT(test, exclusive.has_value());
    CRUCIBLE_TEST_ASSERT(test, exclusive->size() == 4);
    CRUCIBLE_TEST_ASSERT(test, exclusive->data() == storage);
}

void test_two_readers_hold_one_region(TestRunnerCtx const& test) {
    static std::uint64_t storage[3] = {7, 8, 9};
    BgCtx ctx{eff::testing::bg()};

    auto region = ::fixy::mint_owned_region(storage, std::size_t{3}, perm::mint_permission_root<Cache>());
    ::fixy::SharedRegion shared{std::move(region)};

    auto first = shared.lend(ctx).expect("an idle region lends a share");
    auto second = shared.lend(ctx).expect("a region lends a second share");
    CRUCIBLE_TEST_ASSERT(test, shared.outstanding() == 2);

    auto read_a = ::fixy::mint_shared_read(ctx, first, shared);
    auto read_b = ::fixy::mint_shared_read(ctx, second, shared);
    CRUCIBLE_TEST_ASSERT(test, read_a.size() == 3 && read_b.size() == 3);
    CRUCIBLE_TEST_ASSERT(test, read_a[1] == 8 && read_b[1] == 8);
    CRUCIBLE_TEST_ASSERT(test, read_a.cspan().data() == read_b.cspan().data());
}

// The composition, as a type question.  Two regions of one tag minted at
// two sites are two brands, so the guard of the first does not read the
// second even though the share and the row are both in order.
void test_a_share_of_another_region_does_not_read_this_one(TestRunnerCtx const& test) {
    static std::uint64_t storage_a[2] = {1, 2};
    static std::uint64_t storage_b[2] = {3, 4};
    BgCtx ctx{eff::testing::bg()};

    auto region_a = ::fixy::mint_owned_region(storage_a, std::size_t{2}, perm::mint_permission_root<Cache>());
    auto region_b = ::fixy::mint_owned_region(storage_b, std::size_t{2}, perm::mint_permission_root<Cache>());
    static_assert(!::foundation::brand::SameBrand<decltype(region_a), decltype(region_b)>,
                  "two roots at two sites are two brands");

    ::fixy::SharedRegion shared_a{std::move(region_a)};
    ::fixy::SharedRegion shared_b{std::move(region_b)};

    auto guard_a = shared_a.lend(ctx).expect("an idle region lends a share");

    using GuardA = decltype(guard_a);
    using SharedA = std::remove_cvref_t<decltype(shared_a)>;
    using SharedB = std::remove_cvref_t<decltype(shared_b)>;

    static_assert(CanRead<BgCtx, GuardA, SharedA>, "the share of this region reads this region");
    static_assert(!CanRead<BgCtx, GuardA, SharedB>, "and reads no other region of the same tag");

    // The share and the row are both in order for the region it belongs
    // to, which is what makes the refusal the composition's and not one
    // axis's.
    auto read = ::fixy::mint_shared_read(ctx, guard_a, shared_a);
    CRUCIBLE_TEST_ASSERT(test, read[1] == 2);
}

// The effect axis, in the same shape.  A context that does not admit the
// tag's row cannot take the read at all, whatever the share says.
void test_a_context_that_refuses_the_row_refuses_the_read(TestRunnerCtx const& test) {
    using FgCtx = eff::ExecCtx<eff::ctx_cap::Fg, eff::Row<>>;
    using IoCtx = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::IO>>;

    struct cell_brand {};
    static_assert(::fixy::CtxFitsSharedRead<std::uint64_t, Cache, cell_brand, FgCtx>);
    static_assert(!::fixy::CtxFitsSharedRead<std::uint64_t, Spilled, cell_brand, FgCtx>,
                  "a foreground context does not admit a region whose tag says IO");
    static_assert(::fixy::CtxFitsSharedRead<std::uint64_t, Spilled, cell_brand, IoCtx>,
                  "and a context that carries IO does");

    // The cell is a compile-time claim, so the runtime half only proves
    // the claim was instantiated.
    CRUCIBLE_TEST_ASSERT(test, true);
}

// On the erased brand every region of the tag is one type, so the
// deduction would pair any guard with any region.  The gate refuses it.
void test_the_erased_brand_reads_nothing(TestRunnerCtx const& test) {
    static std::uint64_t storage[2] = {5, 6};
    BgCtx ctx{eff::testing::bg()};

    ::fixy::SharedRegion<std::uint64_t, Cache> shared{::fixy::OwnedRegion<std::uint64_t, Cache>::wrap(
        storage, std::size_t{2}, perm::permission_erase_brand(perm::mint_permission_root<Cache>()))};
    auto guard = shared.lend(ctx).expect("an idle region lends a share");

    using Guard = decltype(guard);
    using Shared = std::remove_cvref_t<decltype(shared)>;
    static_assert(!CanRead<BgCtx, Guard, Shared>, "a share on the erased brand reads nothing");
    CRUCIBLE_TEST_ASSERT(test, true);
}

// A brand names a mint site, so two regions built by one statement are
// one type, and so are their guards.  The mint asks the guard at run time
// which pool it holds a share of.
auto region_at_one_site(std::uint64_t* storage) {
    return ::fixy::mint_owned_region(storage, std::size_t{2}, perm::mint_permission_root<Cache>());
}

void test_a_share_of_a_region_of_the_same_site_aborts(TestRunnerCtx const& test) {
    static std::uint64_t storage_a[2] = {7, 8};
    static std::uint64_t storage_b[2] = {9, 10};
    BgCtx ctx{eff::testing::bg()};

    ::fixy::SharedRegion shared_a{region_at_one_site(storage_a)};
    ::fixy::SharedRegion shared_b{region_at_one_site(storage_b)};
    static_assert(std::is_same_v<decltype(shared_a), decltype(shared_b)>, "one site is one brand");

    auto guard_a = shared_a.lend(ctx).expect("an idle region lends a share");
    auto read = ::fixy::mint_shared_read(ctx, guard_a, shared_a);
    CRUCIBLE_TEST_ASSERT(test, read[0] == 7);
    // The read of the other region runs in a child through expect_dies of the
    // Check family.
    ::fixy::check::expect_dies(
        test, [&] noexcept { (void)::fixy::mint_shared_read(ctx, guard_a, shared_b); },
        ::fixy::TextView{"foundation: fatal invariant failed: guard.is_share_of(region.pool_)"});
}

// The door takes the base and the count out of the region it consumes.
// A region that kept them could write the bytes that every share reads.
void test_the_consumed_region_is_empty(TestRunnerCtx const& test) {
    static std::uint64_t storage[3] = {4, 5, 6};
    auto region = ::fixy::mint_owned_region(storage, std::size_t{3}, perm::mint_permission_root<Cache>());
    ::fixy::SharedRegion shared{std::move(region)};
    CRUCIBLE_TEST_ASSERT(test, region.empty());
    CRUCIBLE_TEST_ASSERT(test, region.data() == nullptr);
    CRUCIBLE_TEST_ASSERT(test, region.span().empty());
    CRUCIBLE_TEST_ASSERT(test, shared.size() == 3);
}

}  // namespace

int main() {
    TestRunnerCtx const test{eff::testing::test()};
    pass(test, "test_shared_region:\n");
    test_the_consumed_region_is_empty(test);
    pass(test, "  test_the_consumed_region_is_empty: PASSED\n");
    test_a_read_sees_the_region(test);
    pass(test, "  test_a_read_sees_the_region: PASSED\n");
    test_an_upgrade_waits_for_every_share(test);
    pass(test, "  test_an_upgrade_waits_for_every_share: PASSED\n");
    test_two_readers_hold_one_region(test);
    pass(test, "  test_two_readers_hold_one_region: PASSED\n");
    test_a_share_of_another_region_does_not_read_this_one(test);
    pass(test, "  test_a_share_of_another_region_does_not_read_this_one: PASSED\n");
    test_a_context_that_refuses_the_row_refuses_the_read(test);
    pass(test, "  test_a_context_that_refuses_the_row_refuses_the_read: PASSED\n");
    test_the_erased_brand_reads_nothing(test);
    pass(test, "  test_the_erased_brand_reads_nothing: PASSED\n");
    test_a_share_of_a_region_of_the_same_site_aborts(test);
    pass(test, "  test_a_share_of_a_region_of_the_same_site_aborts: PASSED\n");
    pass(test, "test_shared_region: ALL PASSED\n");
    return 0;
}
