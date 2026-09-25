// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// mint_reader_runtime_session takes the reader handle by move, so the
// session owns it.  An lvalue handle would stay usable beside the
// session, so the mint refuses it.
//
// Expected diagnostic: CtxFitsSwmrReaderSession is not satisfied.

#include <fixy/concurrent/SwmrSession.h>

#include <foundation/effects/Ctx.h>

namespace swmr_reader_session_lvalue_fixture {
struct WriterTag {
    using permission_row = ::foundation::effects::Row<>;
};
struct ReaderTag {
    using permission_row = ::foundation::effects::Row<>;
};
inline auto reader_root() noexcept { return ::foundation::permissions::mint_permission_root<ReaderTag>(); }
using Swmr = ::fixy::concurrent::swmr_session::SwmrSession<int, WriterTag, ReaderTag,
                                                           ::foundation::brand::brand_of_t<decltype(reader_root())>>;
}  // namespace swmr_reader_session_lvalue_fixture

int main() {
    using namespace swmr_reader_session_lvalue_fixture;
    namespace ses = ::fixy::concurrent::swmr_session;
    namespace eff = ::foundation::effects;
    const eff::ExecCtx<eff::ctx_cap::Fg, eff::Row<>> ctx = eff::testing::foreground();
    Swmr swmr{reader_root(), 1};
    auto reader = ses::mint_swmr_reader<Swmr>(swmr);
    auto session = ses::mint_reader_runtime_session<Swmr>(ctx, *reader);
    (void)session;
    return 0;
}
