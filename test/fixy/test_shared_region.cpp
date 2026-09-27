// The three axes as one surface: a read that a brand, a share and an
// effect row all agree on.
//
// The static cells stand on what the type system refuses.  The runtime
// cells stand on what only a running program shows: that a read sees
// the bytes the region holds, that an upgrade waits for every share,
// and that two readers hold one region at once.

#include <fixy/SharedRegion.h>

#include "../foundation/abort_probe.h"

#include <foundation/effects/Effect.h>
#include <foundation/permissions/Permission.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <type_traits>
#include <utility>

namespace eff = ::foundation::effects;
namespace perm = ::foundation::permissions;

struct TestFailure {};

#define CRUCIBLE_TEST_REQUIRE(...)                                                        \
    do {                                                                                  \
        if (!(__VA_ARGS__)) [[unlikely]] {                                                \
            std::fprintf(stderr, "FAIL: %s (%s:%d)\n", #__VA_ARGS__, __FILE__, __LINE__); \
            throw TestFailure{};                                                          \
        }                                                                                 \
    } while (0)

namespace {

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

using BgCtx = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg>>;

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

void test_a_read_sees_the_region() {
    static std::uint64_t storage[6] = {10, 11, 12, 13, 14, 15};
    BgCtx ctx{eff::testing::bg()};

    auto region = ::fixy::mint_owned_region(storage, std::size_t{6}, perm::mint_permission_root<Cache>());
    ::fixy::SharedRegion shared{std::move(region)};
    CRUCIBLE_TEST_REQUIRE(shared.size() == 6);
    CRUCIBLE_TEST_REQUIRE(!shared.empty());

    auto guard = shared.lend(ctx);
    CRUCIBLE_TEST_REQUIRE(guard.has_value());

    auto read = ::fixy::mint_shared_read(ctx, *guard, shared);
    CRUCIBLE_TEST_REQUIRE(read.size() == 6);
    CRUCIBLE_TEST_REQUIRE(read[0] == 10);
    CRUCIBLE_TEST_REQUIRE(read[5] == 15);

    std::uint64_t sum = 0;
    for (std::uint64_t value : read) sum += value;
    CRUCIBLE_TEST_REQUIRE(sum == 75);

    // The read names the instance it was taken against, and the share
    // it stands on is the guard's.
    static_assert(::foundation::brand::SameBrand<decltype(read), decltype(*guard)>);
}

void test_an_upgrade_waits_for_every_share() {
    static std::uint64_t storage[4] = {1, 2, 3, 4};
    BgCtx ctx{eff::testing::bg()};

    auto region = ::fixy::mint_owned_region(storage, std::size_t{4}, perm::mint_permission_root<Cache>());
    ::fixy::SharedRegion shared{std::move(region)};

    {
        auto guard = shared.lend(ctx);
        CRUCIBLE_TEST_REQUIRE(guard.has_value());
        CRUCIBLE_TEST_REQUIRE(shared.outstanding() == 1);
        CRUCIBLE_TEST_REQUIRE(!shared.try_upgrade(ctx).has_value());
    }

    CRUCIBLE_TEST_REQUIRE(shared.outstanding() == 0);
    auto exclusive = shared.try_upgrade(ctx);
    CRUCIBLE_TEST_REQUIRE(exclusive.has_value());
    CRUCIBLE_TEST_REQUIRE(exclusive->size() == 4);
    CRUCIBLE_TEST_REQUIRE(exclusive->data() == storage);
}

void test_two_readers_hold_one_region() {
    static std::uint64_t storage[3] = {7, 8, 9};
    BgCtx ctx{eff::testing::bg()};

    auto region = ::fixy::mint_owned_region(storage, std::size_t{3}, perm::mint_permission_root<Cache>());
    ::fixy::SharedRegion shared{std::move(region)};

    auto first = shared.lend(ctx);
    auto second = shared.lend(ctx);
    CRUCIBLE_TEST_REQUIRE(first.has_value() && second.has_value());
    CRUCIBLE_TEST_REQUIRE(shared.outstanding() == 2);

    auto read_a = ::fixy::mint_shared_read(ctx, *first, shared);
    auto read_b = ::fixy::mint_shared_read(ctx, *second, shared);
    CRUCIBLE_TEST_REQUIRE(read_a.size() == 3 && read_b.size() == 3);
    CRUCIBLE_TEST_REQUIRE(read_a[1] == 8 && read_b[1] == 8);
    CRUCIBLE_TEST_REQUIRE(read_a.cspan().data() == read_b.cspan().data());
}

// The composition, as a type question.  Two regions of one tag minted at
// two sites are two brands, so the guard of the first does not read the
// second even though the share and the row are both in order.
void test_a_share_of_another_region_does_not_read_this_one() {
    static std::uint64_t storage_a[2] = {1, 2};
    static std::uint64_t storage_b[2] = {3, 4};
    BgCtx ctx{eff::testing::bg()};

    auto region_a = ::fixy::mint_owned_region(storage_a, std::size_t{2}, perm::mint_permission_root<Cache>());
    auto region_b = ::fixy::mint_owned_region(storage_b, std::size_t{2}, perm::mint_permission_root<Cache>());
    static_assert(!::foundation::brand::SameBrand<decltype(region_a), decltype(region_b)>,
                  "two roots at two sites are two brands");

    ::fixy::SharedRegion shared_a{std::move(region_a)};
    ::fixy::SharedRegion shared_b{std::move(region_b)};

    auto guard_a = shared_a.lend(ctx);
    CRUCIBLE_TEST_REQUIRE(guard_a.has_value());

    using GuardA = std::remove_cvref_t<decltype(*guard_a)>;
    using SharedA = std::remove_cvref_t<decltype(shared_a)>;
    using SharedB = std::remove_cvref_t<decltype(shared_b)>;

    static_assert(CanRead<BgCtx, GuardA, SharedA>, "the share of this region reads this region");
    static_assert(!CanRead<BgCtx, GuardA, SharedB>, "and reads no other region of the same tag");

    // The share and the row are both in order for the region it belongs
    // to, which is what makes the refusal the composition's and not one
    // axis's.
    auto read = ::fixy::mint_shared_read(ctx, *guard_a, shared_a);
    CRUCIBLE_TEST_REQUIRE(read[1] == 2);
}

// The effect axis, in the same shape.  A context that does not admit the
// tag's row cannot take the read at all, whatever the share says.
void test_a_context_that_refuses_the_row_refuses_the_read() {
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
    CRUCIBLE_TEST_REQUIRE(true);
}

// On the erased brand every region of the tag is one type, so the
// deduction would pair any guard with any region.  The gate refuses it.
void test_the_erased_brand_reads_nothing() {
    static std::uint64_t storage[2] = {5, 6};
    BgCtx ctx{eff::testing::bg()};

    ::fixy::SharedRegion<std::uint64_t, Cache> shared{
        ::fixy::OwnedRegion<std::uint64_t, Cache>::wrap(storage, std::size_t{2}, perm::mint_permission_root<Cache>())};
    auto guard = shared.lend(ctx);
    CRUCIBLE_TEST_REQUIRE(guard.has_value());

    using Guard = std::remove_cvref_t<decltype(*guard)>;
    using Shared = std::remove_cvref_t<decltype(shared)>;
    static_assert(!CanRead<BgCtx, Guard, Shared>, "a share on the erased brand reads nothing");
    CRUCIBLE_TEST_REQUIRE(true);
}

// A brand names a mint site, so two regions built by one statement are
// one type, and so are their guards.  The mint asks the guard at run time
// which pool it holds a share of.
auto region_at_one_site(std::uint64_t* storage) {
    return ::fixy::mint_owned_region(storage, std::size_t{2}, perm::mint_permission_root<Cache>());
}

void test_a_share_of_a_region_of_the_same_site_aborts() {
    static std::uint64_t storage_a[2] = {7, 8};
    static std::uint64_t storage_b[2] = {9, 10};
    BgCtx ctx{eff::testing::bg()};

    ::fixy::SharedRegion shared_a{region_at_one_site(storage_a)};
    ::fixy::SharedRegion shared_b{region_at_one_site(storage_b)};
    static_assert(std::is_same_v<decltype(shared_a), decltype(shared_b)>, "one site is one brand");

    auto guard_a = shared_a.lend(ctx);
    CRUCIBLE_TEST_REQUIRE(guard_a.has_value());
    auto read = ::fixy::mint_shared_read(ctx, *guard_a, shared_a);
    CRUCIBLE_TEST_REQUIRE(read[0] == 7);
    CRUCIBLE_TEST_REQUIRE(::foundation::test::aborts([&] { (void)::fixy::mint_shared_read(ctx, *guard_a, shared_b); }));
}

// The door takes the base and the count out of the region it consumes.
// A region that kept them could write the bytes that every share reads.
void test_the_consumed_region_is_empty() {
    static std::uint64_t storage[3] = {4, 5, 6};
    auto region = ::fixy::mint_owned_region(storage, std::size_t{3}, perm::mint_permission_root<Cache>());
    ::fixy::SharedRegion shared{std::move(region)};
    CRUCIBLE_TEST_REQUIRE(region.empty());
    CRUCIBLE_TEST_REQUIRE(region.data() == nullptr);
    CRUCIBLE_TEST_REQUIRE(region.span().empty());
    CRUCIBLE_TEST_REQUIRE(shared.size() == 3);
}

}  // namespace

int main() {
    std::fprintf(stderr, "test_shared_region:\n");
    run_test("test_the_consumed_region_is_empty", test_the_consumed_region_is_empty);
    run_test("test_a_read_sees_the_region", test_a_read_sees_the_region);
    run_test("test_an_upgrade_waits_for_every_share", test_an_upgrade_waits_for_every_share);
    run_test("test_two_readers_hold_one_region", test_two_readers_hold_one_region);
    run_test("test_a_share_of_another_region_does_not_read_this_one",
             test_a_share_of_another_region_does_not_read_this_one);
    run_test("test_a_context_that_refuses_the_row_refuses_the_read",
             test_a_context_that_refuses_the_row_refuses_the_read);
    run_test("test_the_erased_brand_reads_nothing", test_the_erased_brand_reads_nothing);
    run_test("test_a_share_of_a_region_of_the_same_site_aborts", test_a_share_of_a_region_of_the_same_site_aborts);
    std::fprintf(stderr, "test_shared_region: %d passed, %d failed\n", total_passed, total_failed);
    return total_failed == 0 ? 0 : 1;
}
