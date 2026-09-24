// A read through a SharedReader that stays named.  read consumes the
// reader for the call and hands it back, so the body cannot end the
// share through a capture while the view lives.  A named reader stays
// with the caller, so read refuses it: the member takes an rvalue only.

#include <fixy/session/Payload.h>
#include <foundation/permissions/Permission.h>
#include <foundation/permissions/ReadView.h>

#include <utility>

namespace fp = ::foundation::permissions;
namespace sess = ::fixy::session;

namespace {

struct Region {
    using permission_row = ::foundation::effects::Row<>;
};

}  // namespace

int main() {
    fp::SharedPermissionPool pool{fp::mint_permission_root<Region>()};
    sess::SharedReader reader{std::move(*pool.lend())};
    auto back = reader.read([](auto const&) noexcept {});
    (void)back;
    return 0;
}
