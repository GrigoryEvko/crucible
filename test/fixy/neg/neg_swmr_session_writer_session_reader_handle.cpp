// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// mint_writer_runtime_session runs the writer protocol over the writer
// handle.  A reader handle cannot publish, so the mint refuses it.
//
// Expected diagnostic: CtxFitsSwmrWriterSession is not satisfied.

#include <fixy/concurrent/SwmrSession.h>

#include <foundation/effects/Ctx.h>

#include <utility>

namespace swmr_writer_session_role_fixture {
struct WriterTag {
    using permission_row = ::foundation::effects::Row<>;
};
struct ReaderTag {
    using permission_row = ::foundation::effects::Row<>;
};
inline auto reader_root() noexcept { return ::foundation::permissions::mint_permission_root<ReaderTag>(); }
using Swmr = ::fixy::concurrent::swmr_session::SwmrSession<int, WriterTag, ReaderTag,
                                                           ::foundation::brand::brand_of_t<decltype(reader_root())>>;
}  // namespace swmr_writer_session_role_fixture

int main() {
    using namespace swmr_writer_session_role_fixture;
    namespace ses = ::fixy::concurrent::swmr_session;
    namespace eff = ::foundation::effects;
    const eff::ExecCtx<eff::ctx_cap::Fg, eff::Row<>> ctx = eff::testing::foreground();
    Swmr swmr{reader_root(), 1};
    auto reader = ses::mint_swmr_reader<Swmr>(swmr);
    auto session = ses::mint_writer_runtime_session<Swmr>(ctx, std::move(*reader));
    (void)session;
    return 0;
}
