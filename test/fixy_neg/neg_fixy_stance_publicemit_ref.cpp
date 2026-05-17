// fixy_neg: stance::PublicEmit<int&, Policy> rejects via Type-axis gate.
//
// HS14 floor for FIXY-AUDIT-D4 (PublicEmit stance, Type-axis angle).
// Existing fixtures pin PublicEmit's axis discipline:
//   - neg_fixy_publicemit_with_secret    — hand-rolled as_secret + IO
//   - neg_fixy_publicemit_unengaged_effect — drop the IO engagement
// This fixture adds the Type-axis angle: a reference Type is not an
// object; IsAccepted rejects before any of the publish-discipline
// axes are evaluated.
//
// Expected diagnostic: "IsAccepted".

#include <crucible/fixy/Fn.h>
#include <crucible/safety/Secret.h>

namespace stance = crucible::fixy::stance;
namespace policy = crucible::safety::secret_policy;

using BadPublicEmitRef = stance::PublicEmit<int&, policy::UserDisplay>;

static_assert(sizeof(BadPublicEmitRef) > 0,
    "instantiate stance::PublicEmit<int&, UserDisplay> to force the "
    "Type-axis rejection (references are not objects).");

int main() { return 0; }
