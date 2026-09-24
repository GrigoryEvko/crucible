// A Released built with no read loan.  A release carries the loan back,
// and the lender gets its token only from the parked token and that
// loan together.  A release with no loan would reopen a region whose
// loan is still out, so Released has no constructor that takes the value
// alone.

#include <fixy/session/Payload.h>
#include <foundation/permissions/Permission.h>
#include <foundation/permissions/ReadView.h>

namespace sess = ::fixy::session;

namespace {

struct Region {
    using permission_row = ::foundation::effects::Row<>;
};

}  // namespace

int main() {
    sess::Released<int, Region> message{1};
    return message.value;
}
