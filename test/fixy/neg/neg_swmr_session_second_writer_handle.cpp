// Negative compile test.  This file must fail to compile.
//
// The writer end of a session is the one writer of its snapshot, so the
// snapshot takes no run-time test for a second writer.  A copy of the writer
// handle would give the session a second writer end, and the two would race in
// the seqlock.  The handle owns the linear writer permission, so it has no
// copy.

#include <fixy/channels/SwmrSession.h>

#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include <utility>

namespace swmr_second_writer_fixture {
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
using WriterToken = ::foundation::permissions::Permission<WriterTag, Swmr::writer_brand>;
}  // namespace swmr_second_writer_fixture

int main() {
    using namespace swmr_second_writer_fixture;
    namespace ses = ::fixy::channels::swmr_session;
    Swmr swmr{reader_root(), 1};
    auto token = ses::with_writer(swmr, writer_root(), [](auto first) noexcept -> WriterToken {
        auto second = first;
        second.publish(3);
        first.publish(2);
        return std::move(first).release();
    });
    return 0;
}
