// The scope mint takes no context, so it gives only a token of a tag with the
// empty row.  A tag whose row names IO needs a context that admits IO, and the
// mint refuses it, also from the door of the tag.

#include <foundation/effects/Effect.h>
#include <foundation/effects/Row.h>
#include <foundation/permissions/Permission.h>

namespace {

struct IoDoor;

struct IoTag final {
    using permission_row = ::foundation::effects::Row<::foundation::effects::Effect::IO>;
    using scope_door = IoDoor;
};

struct IoBrand final {};

struct IoDoor final {
    static int open() noexcept {
        ::foundation::permissions::scope_mint_key<IoTag> const key{};
        auto token = ::foundation::permissions::mint_scope_permission<IoTag, IoBrand>(key);
        static_cast<void>(token);
        return 0;
    }
};

}  // namespace

int main() { return IoDoor::open(); }
