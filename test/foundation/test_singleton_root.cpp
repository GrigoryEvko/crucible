// The singleton root of a permission tag: the one live root of the tag on
// the erased identity.  A release clears the live count of the tag, so a
// later mint takes the tag again.  The count of one tag says nothing about
// another tag.  A second live singleton root of one tag ends the process,
// and that mint runs in a child process through expect_dies of the Check
// family.

#include <foundation/Brand.h>
#include <foundation/Linear.h>
#include <foundation/core/Text.h>
#include <foundation/effects/Effect.h>
#include <foundation/permissions/Permission.h>

#include <fixy/Ctx.h>
#include <fixy/check/Check.h>

#include <type_traits>
#include <utility>

// A tag of a singleton root has a stable identity, so it has external
// linkage.
namespace test_singleton_root_tags {
struct Region {
    using permission_row = ::foundation::effects::Row<>;
};
struct Other {
    using permission_row = ::foundation::effects::Row<>;
};
struct IoRegion {
    using permission_row = ::foundation::effects::Row<::foundation::effects::Effect::IO>;
};
}  // namespace test_singleton_root_tags

namespace {

namespace fp = ::foundation::permissions;

using test_singleton_root_tags::IoRegion;
using test_singleton_root_tags::Other;
using test_singleton_root_tags::Region;

// A class with internal linkage has no stable identity, so the registry of
// the live counts has no key for it, and the door refuses it.
struct InternalRegion {
    using permission_row = ::foundation::effects::Row<>;
};
static_assert(fp::SingletonRootTag<Region> && !fp::SingletonRootTag<InternalRegion>);

// The root is on the erased identity of its tag, and a release clears the
// count, so the next mint of the tag succeeds.
void release_clears_the_count() {
    auto first = fp::mint_singleton_root<Region>();
    static_assert(::foundation::brand::IsErased<decltype(first)>);
    static_assert(fp::IsPermissionFor<decltype(first), Region>);
    fp::release_singleton_root(std::move(first));
    auto second = fp::mint_singleton_root<Region>();
    fp::release_singleton_root(std::move(second));
}

// Each tag has its own count, so a live root of one tag does not stop the
// mint of another.
void tags_count_apart() {
    auto region = fp::mint_singleton_root<Region>();
    auto other = fp::mint_singleton_root<Other>();
    fp::release_singleton_root(std::move(other));
    fp::release_singleton_root(std::move(region));
}

// An effectful tag mints its singleton root under a context that admits its
// row, as the plain root mint does.
void ctx_admits_the_row(::fixy::TestRunnerCtx const& ctx) {
    auto io = fp::mint_singleton_root<IoRegion>(ctx);
    fp::release_singleton_root(std::move(io));
}

// A plain root of the tag does not take the count, so it lives beside the
// singleton root, on a brand of its own.
void plain_root_keeps_its_brand() {
    auto plain = fp::mint_permission_root<Region>();
    auto single = fp::mint_singleton_root<Region>();
    static_assert(!std::is_same_v<decltype(plain), decltype(single)>);
    fp::release_singleton_root(std::move(single));
    ::foundation::linear::drop(std::move(plain));
}

void mint_two_live_roots() {
    auto first = fp::mint_singleton_root<Region>();
    auto second = fp::mint_singleton_root<Region>();
    fp::release_singleton_root(std::move(second));
    fp::release_singleton_root(std::move(first));
}

}  // namespace

int main() {
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    release_clears_the_count();
    tags_count_apart();
    ctx_admits_the_row(test);
    plain_root_keeps_its_brand();

    ::fixy::check::expect_dies(
        test, mint_two_live_roots,
        ::foundation::core::TextView{"LinearityViolation: a second live singleton root of the permission tag"});
    // The count stays clear in this process, because the second mint ran in
    // the child.
    release_clears_the_count();
    ::fixy::check::pass(test, "test_singleton_root: OK\n");
    return 0;
}
