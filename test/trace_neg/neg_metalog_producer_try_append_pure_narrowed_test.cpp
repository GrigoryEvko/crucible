// Negative-compile test.  This file must fail to compile.
//
// The pure append of a producer endpoint of PermissionedMetaLog promises a
// pure caller.  A test runner narrows its context to the empty row with
// in_row, and the narrowed context keeps the source of the test runner, which
// permits atoms.  The gate reads the source as well as the row, so the
// narrowed context fails the gate.
//
// Expected diagnostic: CtxHasPureSource of the narrowed context is not
// satisfied.

#include <crucible/MetaLog.h>
#include <crucible/PermissionedMetaLog.h>
#include <fixy/Ctx.h>

#include <fixy/concurrent/PermissionedChannel.h>
#include <foundation/Linear.h>
#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include <utility>

namespace {
struct Tag {};
}  // namespace

int main() {
    const ::fixy::TestRunnerCtx test{::foundation::effects::testing::test()};
    auto root = ::foundation::permissions::mint_permission_root<::crucible::metalog_tag::Whole<Tag>>();
    using Log = ::crucible::permissioned_metalog_t<decltype(root)>;
    ::crucible::MetaLog raw{::foundation::effects::testing::test().alloc};
    Log log{raw};
    auto tokens =
        ::foundation::permissions::mint_permission_split<Log::producer_tag, Log::consumer_tag>(std::move(root));
    ::foundation::linear::drop(std::move(tokens.at(::foundation::core::index<1>())));
    ::fixy::concurrent::with_producer(
        log, std::move(tokens.at(::foundation::core::index<0>())), [&test](auto producer) noexcept -> void {
            const ::fixy::FixedArray<::crucible::TensorMeta, 1> record{};
            (void)producer.try_append_pure(test.in_row<::foundation::effects::Row<>>(), record.view());
            ::foundation::linear::drop(std::move(producer).release());
        });
    return 0;
}
