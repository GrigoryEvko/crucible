// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// mint_reader_runtime_session runs the reader protocol over a reader
// handle.  A writer handle cannot load, so the mint refuses it.
//
// Expected diagnostic: CtxFitsSwmrReaderSession is not satisfied.

#include <fixy/concurrent/SwmrSession.h>

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
using Swmr = ::fixy::concurrent::swmr_session::SwmrSession<int, WriterTag, ReaderTag,
                                                           ::foundation::brand::brand_of_t<decltype(reader_root())>,
                                                           ::foundation::brand::brand_of_t<decltype(writer_root())>>;
}  // namespace swmr_reader_session_role_fixture

int main() {
    using namespace swmr_reader_session_role_fixture;
    namespace ses = ::fixy::concurrent::swmr_session;
    namespace eff = ::foundation::effects;
    namespace perm = ::foundation::permissions;
    const eff::ExecCtx<eff::ctx_cap::Fg, eff::Row<>> ctx = eff::testing::foreground();
    Swmr swmr{reader_root(), 1};
    auto writer = ses::mint_swmr_writer<Swmr>(swmr, writer_root());
    auto session = ses::mint_reader_runtime_session<Swmr>(ctx, std::move(writer));
    (void)session;
    return 0;
}
