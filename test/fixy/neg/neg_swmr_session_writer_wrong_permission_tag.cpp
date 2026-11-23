// Negative compile test.  This file must fail to compile.
//
// The writer door with_writer takes the writer permission of its session.
// A permission of the reader tag is not the writer permission, so the door
// refuses it.
//
// Expected diagnostic: no with_writer overload takes the permission.

#include <fixy/channels/SwmrSession.h>

#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include <utility>

namespace swmr_writer_tag_fixture {
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
}  // namespace swmr_writer_tag_fixture

int main() {
    using namespace swmr_writer_tag_fixture;
    namespace ses = ::fixy::channels::swmr_session;
    namespace perm = ::foundation::permissions;
    Swmr swmr{reader_root(), 1};
    auto token = ses::with_writer(swmr, perm::mint_permission_root<ReaderTag>(), [](auto writer) noexcept {
        writer.publish(2);
        return std::move(writer).release();
    });
    return 0;
}
