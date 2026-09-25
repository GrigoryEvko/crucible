// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// mint_writer_runtime_session takes the writer handle by move, so the
// session owns it.  An lvalue handle would stay usable beside the
// session, so the mint refuses it.
//
// Expected diagnostic: CtxFitsSwmrWriterSession is not satisfied.

#include <fixy/concurrent/SwmrSession.h>

#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

namespace swmr_writer_session_lvalue_fixture {
struct WriterTag {
    using permission_row = ::foundation::effects::Row<>;
};
struct ReaderTag {
    using permission_row = ::foundation::effects::Row<>;
};
inline auto reader_root() noexcept { return ::foundation::permissions::mint_permission_root<ReaderTag>(); }
using Swmr = ::fixy::concurrent::swmr_session::SwmrSession<int, WriterTag, ReaderTag,
                                                           ::foundation::brand::brand_of_t<decltype(reader_root())>>;
}  // namespace swmr_writer_session_lvalue_fixture

int main() {
    using namespace swmr_writer_session_lvalue_fixture;
    namespace ses = ::fixy::concurrent::swmr_session;
    namespace eff = ::foundation::effects;
    namespace perm = ::foundation::permissions;
    const eff::ExecCtx<eff::ctx_cap::Fg, eff::Row<>> ctx = eff::testing::foreground();
    Swmr swmr{reader_root(), 1};
    auto writer = ses::mint_swmr_writer<Swmr>(swmr, perm::mint_permission_root<WriterTag>());
    auto session = ses::mint_writer_runtime_session<Swmr>(ctx, writer);
    (void)session;
    return 0;
}
