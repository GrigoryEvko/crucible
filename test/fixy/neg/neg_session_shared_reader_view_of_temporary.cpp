// A read proof taken from a SharedReader that dies at the end of the
// statement.  The share ends with the reader, so the proof outlives the
// share it proves.  The deleted rvalue twin of view() refuses the call.

#include <fixy/session/Payload.h>
#include <foundation/permissions/Permission.h>
#include <foundation/permissions/ReadView.h>

#include <functional>
#include <optional>
#include <utility>

namespace fp = ::foundation::permissions;
namespace sess = ::fixy::session;

namespace {
struct Region {
    using permission_row = ::foundation::effects::Row<>;
};
struct Other {
    using permission_row = ::foundation::effects::Row<>;
};
using TX = sess::Transferable<int, Region>;
}  // namespace

int main() {
    fp::SharedPermissionPool pool{fp::mint_permission_root<Region>()};
    auto proof = sess::SharedReader{std::move(*pool.lend())}.view();
    (void)proof;
    return 0;
}
