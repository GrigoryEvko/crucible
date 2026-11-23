// Negative compile test.  This file must fail to compile.
//
// The writer door with_writer asks for a session of the SWMR shape.  A type
// that names the member types of a session but has no writer() and no
// reader() is not a session, so the door refuses it.
//
// Expected diagnostic: SwmrSessionSurface is not satisfied.

#include <fixy/channels/SwmrSession.h>

#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include <utility>

namespace swmr_writer_surface_fixture {
struct WriterTag {
    using permission_row = ::foundation::effects::Row<>;
};
struct ReaderTag {
    using permission_row = ::foundation::effects::Row<>;
};
// The fake builds no session, so two named brands stand in for the brands of
// the two root sites.
struct WriterBrand {};
struct ReaderBrand {};
struct NotAChannel {
    using value_type = int;
    using writer_tag = WriterTag;
    using writer_brand = WriterBrand;
    using reader_tag = ReaderTag;
    using reader_brand = ReaderBrand;
    struct WriterEnd {};
    struct ReaderEnd {};
};
}  // namespace swmr_writer_surface_fixture

int main() {
    using namespace swmr_writer_surface_fixture;
    namespace ses = ::fixy::channels::swmr_session;
    namespace perm = ::foundation::permissions;
    NotAChannel fake{};
    auto token = ses::with_writer(fake, perm::mint_permission_root<WriterTag>(),
                                  [](auto writer) noexcept { return std::move(writer).release(); });
    return 0;
}
