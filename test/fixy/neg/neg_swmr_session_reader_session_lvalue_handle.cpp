// Negative compile test.  This file must fail to compile.
//
// mint_reader_runtime_session takes the reader handle by move, so the
// session owns it.  An lvalue handle would stay usable beside the
// session, so the mint refuses it.  The context holds Nondet, as a reader
// session asks, so only the lvalue fails.
//
// Expected diagnostic: CtxFitsSwmrReaderSession is not satisfied.

#include <fixy/Ctx.h>
#include <fixy/channels/SwmrSession.h>

#include <foundation/effects/Ctx.h>

namespace swmr_reader_session_lvalue_fixture {
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
}  // namespace swmr_reader_session_lvalue_fixture

int main() {
    using namespace swmr_reader_session_lvalue_fixture;
    namespace ses = ::fixy::channels::swmr_session;
    namespace eff = ::foundation::effects;
    const ::fixy::TestRunnerCtx ctx{eff::testing::test()};
    Swmr swmr{reader_root(), 1};
    auto lent = ses::with_reader(ctx, swmr, [&ctx](auto reader) noexcept {
        auto session = ses::mint_reader_runtime_session<Swmr>(ctx, reader);
        (void)session;
    });
    return lent.is_some() ? 0 : 1;
}
