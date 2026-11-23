// Negative compile test.  This file must fail to compile.
//
// Each step of a reader session reads the snapshot with try_load, which
// takes the bare tag of Nondet from the context of the step.  So the mint of
// a reader session takes a context that holds Nondet.  The foreground
// context holds no atom, so the mint refuses it.
//
// Expected diagnostic: CtxFitsSwmrReaderSession is not satisfied, because the
// context owns no Nondet.

#include <fixy/Ctx.h>
#include <fixy/channels/SwmrSession.h>

#include <foundation/effects/Ctx.h>

#include <utility>

namespace swmr_reader_session_nondet_fixture {
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
}  // namespace swmr_reader_session_nondet_fixture

int main() {
    using namespace swmr_reader_session_nondet_fixture;
    namespace ses = ::fixy::channels::swmr_session;
    namespace eff = ::foundation::effects;
    const ::fixy::TestRunnerCtx reader_ctx{eff::testing::test()};
    const eff::ExecCtx<eff::ctx_cap::Fg, eff::Row<>> ctx = eff::testing::foreground();
    Swmr swmr{reader_root(), 1};
    auto reader = ses::mint_swmr_reader<Swmr>(reader_ctx, swmr).expect("the pool lends a share");
    auto session = ses::mint_reader_runtime_session<Swmr>(ctx, std::move(reader));
    (void)session;
    return 0;
}
