// The scope mint gives a token on the scope brand of one call of a door.  The
// erased identity DefaultBrand names no call, and each token of the tag on it
// is one type, so the mint refuses it, also from the door of the tag.

#include <foundation/Brand.h>
#include <foundation/effects/Row.h>
#include <foundation/permissions/Permission.h>

namespace {

struct ErasedDoor;

struct ErasedTag final {
    using permission_row = ::foundation::effects::Row<>;
    using scope_door = ErasedDoor;
};

struct ErasedDoor final {
    static int open() noexcept {
        ::foundation::permissions::scope_mint_key<ErasedTag> const key{};
        auto token =
            ::foundation::permissions::mint_scope_permission<ErasedTag, ::foundation::brand::DefaultBrand>(key);
        static_cast<void>(token);
        return 0;
    }
};

}  // namespace

int main() { return ErasedDoor::open(); }
