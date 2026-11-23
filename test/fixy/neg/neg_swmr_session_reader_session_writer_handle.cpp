// Negative compile test.  This file must fail to compile.
//
// mint_reader_runtime_session runs the reader protocol over a reader
// handle.  A writer handle cannot load, so the mint refuses it.  The
// context holds Nondet, as a reader session asks, so only the role fails.
//
// Expected diagnostic: CtxFitsSwmrReaderSession is not satisfied.

#include <fixy/Ctx.h>
#include <fixy/channels/SwmrSession.h>

#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include <utility>

namespace swmr_reader_session_role_fixture {
struct WriterTag {
    using permission_row = ::foundation::effects::Row<>;
};
struct ReaderTag {
    using permission_row = ::foundation::effects::Row<>;
};
inline auto reader_root() noexcept { return ::foundation::permissions::mint_permission_root<ReaderTag>(); }
inline auto writer_root() noexcept { return ::foundation::permissions::mint_permission_root<WriterTag>(); }
using Swmr = ::fixy::channels::swmr_session::SwmrSession<int, WriterTag, ReaderTag,
                                                         ::foundation::brand::brand_of_t<decltype(reader_root())>,
                                                         ::foundation::brand::brand_of_t<decltype(writer_root())>>;
}  // namespace swmr_reader_session_role_fixture

int main() {
    using namespace swmr_reader_session_role_fixture;
    namespace ses = ::fixy::channels::swmr_session;
    namespace eff = ::foundation::effects;
    const ::fixy::TestRunnerCtx ctx{eff::testing::test()};
    Swmr swmr{reader_root(), 1};
    ses::with_writer(swmr, writer_root(), [&ctx](auto writer) noexcept {
        auto session = ses::mint_reader_runtime_session<Swmr>(ctx, std::move(writer));
        (void)session;
    });
    return 0;
}
