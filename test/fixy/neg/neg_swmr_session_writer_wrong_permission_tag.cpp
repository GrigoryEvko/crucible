// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// mint_swmr_writer takes the writer permission of its channel.  A
// permission of the reader tag is not the writer permission, so the mint
// refuses it.
//
// Expected diagnostic: no mint_swmr_writer overload takes the permission.

#include <fixy/concurrent/SwmrSession.h>

#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

namespace swmr_writer_tag_fixture {
struct WriterTag {
    using permission_row = ::foundation::effects::Row<>;
};
struct ReaderTag {
    using permission_row = ::foundation::effects::Row<>;
};
inline auto reader_root() noexcept { return ::foundation::permissions::mint_permission_root<ReaderTag>(); }
using Swmr = ::fixy::concurrent::swmr_session::SwmrSession<int, WriterTag, ReaderTag,
                                                           ::foundation::brand::brand_of_t<decltype(reader_root())>>;
}  // namespace swmr_writer_tag_fixture

int main() {
    using namespace swmr_writer_tag_fixture;
    namespace ses = ::fixy::concurrent::swmr_session;
    namespace perm = ::foundation::permissions;
    Swmr swmr{reader_root(), 1};
    auto writer = ses::mint_swmr_writer<Swmr>(swmr, perm::mint_permission_root<ReaderTag>());
    writer.publish(2);
    return 0;
}
