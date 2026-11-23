// Negative compile test.  This file must fail to compile.
//
// A single-writer session names the brand of its writer root in its type.
// A writer root minted at another site has another brand, so its writer
// door does not open a second writer of the session while the first lives.
// Two writers would race in the seqlock, and a reader could take a byte mix
// of the two values.

#include <fixy/channels/SwmrSession.h>

#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include <utility>

namespace swmr_writer_brand_fixture {
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
}  // namespace swmr_writer_brand_fixture

int main() {
    using namespace swmr_writer_brand_fixture;
    namespace ses = ::fixy::channels::swmr_session;
    namespace perm = ::foundation::permissions;
    Swmr swmr{reader_root(), 1};
    auto token = ses::with_writer(swmr, writer_root(), [&swmr](auto first) noexcept {
        auto second_token = ses::with_writer(swmr, perm::mint_permission_root<WriterTag>(), [](auto second) noexcept {
            second.publish(3);
            return std::move(second).release();
        });
        first.publish(2);
        return std::move(first).release();
    });
    return 0;
}
