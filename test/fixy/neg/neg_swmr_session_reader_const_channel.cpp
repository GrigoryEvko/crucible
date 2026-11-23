// Negative compile test.  This file must fail to compile.
//
// The reader door with_reader lends a share of the reader pool of the
// session, which changes the pool.  A const session cannot lend a share, so
// the door refuses it.  The context holds Nondet, so only the const session
// fails.
//
// Expected diagnostic: SwmrSessionSurface is not satisfied for the const
// session.

#include <fixy/Ctx.h>
#include <fixy/channels/SwmrSession.h>

#include <foundation/effects/Ctx.h>

#include <utility>

namespace swmr_reader_const_fixture {
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
}  // namespace swmr_reader_const_fixture

int main() {
    using namespace swmr_reader_const_fixture;
    namespace ses = ::fixy::channels::swmr_session;
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    Swmr swmr{reader_root(), 1};
    auto lent = ses::with_reader(ctx, std::as_const(swmr), [](auto reader) noexcept { return reader.version(); });
    return lent.is_some() ? 0 : 1;
}
