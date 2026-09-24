// A handle at End with an open loan in its set would lose the loan when
// it closes.  The End class refuses that set when it is built, so no step
// can land on it.  An owned tag in the set is not refused: the token of
// that tag travelled in a payload, and the value that carried it holds it.

#include <fixy/session/Handle.h>

#include <foundation/effects/Row.h>
#include <foundation/permissions/PermSet.h>

namespace {
namespace s = ::fixy::session;
struct Region {
    using permission_row = ::foundation::effects::Row<>;
};
struct Wire {};
using Refused = s::Handle<s::End, ::foundation::permissions::PermSet<s::LentOut<Region>>, Wire>;
}  // namespace

static_assert(sizeof(Refused) > 0);

int main() { return 0; }
