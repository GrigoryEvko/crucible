// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A claim door gives the token of its class: an Option of a handle whose own
// member is the share guard of a tag of the class.  The door lend gives a
// handle that holds the share guard of another tag, which the class does not
// declare as its own, so the guard proves nothing about the class, and the
// door stays a door with no proof (THE DOORS OF A SYNCHRONIZED CLASS of
// foundation/Transfer.h).
//
// Expected diagnostic: a door is not const and takes no proof, and the
// diagnostic names the door lend.

#include <foundation/Transfer.h>
#include <foundation/core/Choice.h>
#include <foundation/effects/Row.h>
#include <foundation/permissions/SharedPermission.h>

namespace {

struct OtherTag {
    using permission_row = ::foundation::effects::Row<>;
};
struct OtherBrand final {};

class[[= ::foundation::transfer::synchronized{}]] Counter {
public:
    Counter() = default;
    Counter(Counter const&) = delete("a count of two threads does not copy");
    Counter& operator=(Counter const&) = delete("a count of two threads does not copy");

    struct Handle {
        ::foundation::permissions::SharedPermissionGuard<OtherTag, OtherBrand> share;
    };

    [[nodiscard]] ::foundation::core::Option<Handle> lend() noexcept;

private:
    int count_ = 0;
};

}  // namespace

static_assert(::foundation::transfer::Synchronized<Counter>);

int main() { return 0; }
