// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// mint_swmr_writer asks for a channel of the SWMR shape.  A type that
// names the channel's member types but has no writer() and no reader()
// is not a channel, so the mint refuses it.
//
// Expected diagnostic: SwmrSessionSurface is not satisfied.

#include <fixy/concurrent/SwmrSession.h>

#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

namespace swmr_writer_surface_fixture {
struct WriterTag {
    using permission_row = ::foundation::effects::Row<>;
};
struct ReaderTag {
    using permission_row = ::foundation::effects::Row<>;
};
struct NotAChannel {
    using value_type = int;
    using writer_tag = WriterTag;
    using reader_tag = ReaderTag;
    struct WriterHandle {};
    struct ReaderHandle {};
};
}  // namespace swmr_writer_surface_fixture

int main() {
    using namespace swmr_writer_surface_fixture;
    namespace ses = ::fixy::concurrent::swmr_session;
    namespace perm = ::foundation::permissions;
    NotAChannel fake{};
    auto writer = ses::mint_swmr_writer<NotAChannel>(fake, perm::mint_permission_root<WriterTag>());
    (void)writer;
    return 0;
}
