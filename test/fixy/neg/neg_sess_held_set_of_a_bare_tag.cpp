// A held set names the token object of each region with its brand.  A bare
// tag names a region and no brand, so the slots of a set of a bare tag name
// no object, and the build stops.  A bare tag that meant a token on the
// erased identity would hand out a token of the type of a singleton root.

#include <fixy/session/Flow.h>
#include <foundation/effects/Effect.h>
#include <foundation/permissions/PermSet.h>

namespace neg_sess_held_set_of_a_bare_tag_types {
struct Region {
    using permission_row = ::foundation::effects::Row<>;
};
}  // namespace neg_sess_held_set_of_a_bare_tag_types

int main() {
    using Slots =
        ::fixy::session::HoldSlots<::foundation::permissions::PermSet<neg_sess_held_set_of_a_bare_tag_types::Region>>;
    static_assert(sizeof(Slots) != 0);
    return 0;
}
