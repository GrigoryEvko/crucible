// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// mint_swmr_reader lends a share of the channel's reader pool, which
// changes the pool.  A const channel cannot lend a share, so the mint
// refuses it.
//
// Expected diagnostic: SwmrSessionSurface is not satisfied for the const
// channel.

#include <fixy/concurrent/SwmrSession.h>

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
using Swmr = ::fixy::concurrent::swmr_session::SwmrSession<int, WriterTag, ReaderTag,
                                                           ::foundation::brand::brand_of_t<decltype(reader_root())>>;
}  // namespace swmr_reader_const_fixture

int main() {
    using namespace swmr_reader_const_fixture;
    namespace ses = ::fixy::concurrent::swmr_session;
    Swmr swmr{reader_root(), 1};
    auto reader = ses::mint_swmr_reader(std::as_const(swmr));
    (void)reader;
    return 0;
}
