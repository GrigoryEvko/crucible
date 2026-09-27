// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A single-writer session names the brand of its writer root in its type.
// A writer root minted at another site has another brand, so it cannot
// become a second writer of the session.  Two writers would race in the
// seqlock, and a reader could take a byte mix of the two values.

#include <fixy/concurrent/SwmrSession.h>

#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

namespace swmr_writer_brand_fixture {
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
}  // namespace swmr_writer_brand_fixture

int main() {
    using namespace swmr_writer_brand_fixture;
    namespace perm = ::foundation::permissions;
    Swmr swmr{reader_root(), 1};
    auto first = swmr.writer(writer_root());
    auto second = swmr.writer(perm::mint_permission_root<WriterTag>());
    first.publish(2);
    second.publish(3);
    return 0;
}
