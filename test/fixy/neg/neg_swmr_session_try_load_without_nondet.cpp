// Negative compile test.  This file must fail to compile.
//
// The read with no wait of a reader handle gives the last publish, or no
// value while a write is in flight.  The schedule of the writer decides the
// value and the outcome, so try_load takes the tag of Nondet.  A call with
// no tag finds no function.
//
// Expected diagnostic: no matching function for try_load().

#include <fixy/Ctx.h>
#include <fixy/channels/SwmrSession.h>

#include <foundation/effects/Ctx.h>

#include <utility>

namespace swmr_try_load_nondet_fixture {
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
}  // namespace swmr_try_load_nondet_fixture

int main() {
    using namespace swmr_try_load_nondet_fixture;
    namespace ses = ::fixy::channels::swmr_session;
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    Swmr swmr{reader_root(), 1};
    auto lent = ses::with_reader(ctx, swmr, [](auto reader) noexcept {
        auto value = reader.try_load();
        (void)value;
    });
    return lent.is_some() ? 0 : 1;
}
