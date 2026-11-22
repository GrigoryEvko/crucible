// Negative-compile test.  This file must fail to compile.
//
// The pure append of a producer endpoint of PermissionedMetaLog reads the row
// of the context of the caller, and the gate IsPure asks that the row is
// empty.  The context of a test runner holds Test, Alloc, IO, Block and
// Nondet, so it fails the gate.
//
// Expected diagnostic: IsPure of the row of the context is not satisfied.

#include <crucible/MetaLog.h>
#include <crucible/PermissionedMetaLog.h>
#include <fixy/Ctx.h>

#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include <utility>

namespace {
struct Tag {};
auto log_root() noexcept {
    return ::foundation::permissions::mint_permission_root<::crucible::metalog_tag::Whole<Tag>>();
}
using Log = ::crucible::permissioned_metalog_t<decltype(log_root())>;
}  // namespace

int main() {
    const ::fixy::TestRunnerCtx test{::foundation::effects::testing::test()};
    ::crucible::MetaLog raw{::foundation::effects::testing::test().alloc};
    Log log{raw};
    auto [pp, cp] = ::foundation::permissions::mint_permission_split<Log::producer_tag, Log::consumer_tag>(log_root());
    (void)cp;
    auto producer = log.producer(std::move(pp));
    const ::fixy::FixedArray<::crucible::TensorMeta, 1> record{};
    (void)producer.try_append_pure(test, record.view());
    return 0;
}
