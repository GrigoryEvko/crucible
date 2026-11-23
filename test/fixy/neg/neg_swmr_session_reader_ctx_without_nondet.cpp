// Negative compile test.  This file must fail to compile.
//
// The reader door with_reader lends a share of the reader pool.  The lend
// fails while the reader root is out, and the schedule decides when the root
// comes back, so the door takes a context that holds Nondet.  The foreground
// context holds no atom, so the door refuses it.
//
// Expected diagnostic: PoolCtx is not satisfied, because the context owns no
// Nondet.

#include <fixy/channels/SwmrSession.h>

#include <foundation/effects/Ctx.h>

namespace swmr_reader_nondet_fixture {
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
}  // namespace swmr_reader_nondet_fixture

int main() {
    using namespace swmr_reader_nondet_fixture;
    namespace ses = ::fixy::channels::swmr_session;
    namespace eff = ::foundation::effects;
    const eff::ExecCtx<eff::ctx_cap::Fg, eff::Row<>> ctx = eff::testing::foreground();
    Swmr swmr{reader_root(), 1};
    auto lent = ses::with_reader(ctx, swmr, [](auto reader) noexcept { return reader.version(); });
    return lent.is_some() ? 0 : 1;
}
