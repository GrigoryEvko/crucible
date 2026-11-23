// Negative compile test.  This file must fail to compile.
//
// mint_writer_runtime_session runs the writer protocol over the writer
// handle.  A reader handle cannot publish, so the mint refuses it.
//
// Expected diagnostic: CtxFitsSwmrWriterSession is not satisfied.

#include <fixy/Ctx.h>
#include <fixy/channels/SwmrSession.h>

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
inline auto writer_root() noexcept { return ::foundation::permissions::mint_permission_root<WriterTag>(); }
using Swmr = ::fixy::channels::swmr_session::SwmrSession<int, WriterTag, ReaderTag,
                                                         ::foundation::brand::brand_of_t<decltype(reader_root())>,
                                                         ::foundation::brand::brand_of_t<decltype(writer_root())>>;
}  // namespace swmr_writer_session_role_fixture

int main() {
    using namespace swmr_writer_session_role_fixture;
    namespace ses = ::fixy::channels::swmr_session;
    namespace eff = ::foundation::effects;
    const eff::ExecCtx<eff::ctx_cap::Fg, eff::Row<>> ctx = eff::testing::foreground();
    // The lend of the reader share takes Nondet, so the reader door takes the
    // context of the test.
    const ::fixy::TestRunnerCtx reader_ctx{eff::testing::test()};
    Swmr swmr{reader_root(), 1};
    auto lent = ses::with_reader(reader_ctx, swmr, [&ctx](auto reader) noexcept {
        auto session = ses::mint_writer_runtime_session<Swmr>(ctx, std::move(reader));
        (void)session;
    });
    return lent.is_some() ? 0 : 1;
}
