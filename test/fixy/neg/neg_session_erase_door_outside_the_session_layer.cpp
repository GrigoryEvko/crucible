// The session erase door drops the brand of a token at an entry of a session
// hold.  Its one member is private, and its one friend is the hold factory of
// fixy/session/Flow.h, so no code outside the session layer drops the brand
// of a root through it.

#include <fixy/session/Flow.h>
#include <foundation/Linear.h>
#include <foundation/permissions/Permission.h>

#include <utility>

namespace {
struct Region {
    using permission_row = ::foundation::effects::Row<>;
};
}  // namespace

int main() {
    auto erased =
        ::foundation::permissions::SessionEraseDoor::erase_(::foundation::permissions::mint_permission_root<Region>());
    ::foundation::linear::drop(std::move(erased));
    return 0;
}
