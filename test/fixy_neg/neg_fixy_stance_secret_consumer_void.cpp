// fixy_neg: stance::SecretConsumer<void, Policy> rejects via Type-axis gate.
//
// HS14 floor for FIXY-AUDIT-D4 (SecretConsumer stance, distinct angle).
// `void` as the declassified payload makes no sense semantically and
// is rejected structurally by IsAccepted's type-axis check.  Distinct
// from the array fixture: array-decay vs void-as-non-object.
//
// Expected diagnostic: "IsAccepted".

#include <crucible/fixy/Fn.h>
#include <crucible/safety/Secret.h>

namespace stance = crucible::fixy::stance;
namespace policy = crucible::safety::secret_policy;

using BadSecretVoid = stance::SecretConsumer<void, policy::WireSerialize>;

static_assert(sizeof(BadSecretVoid) > 0,
    "instantiate stance::SecretConsumer<void, WireSerialize> to "
    "force the Type-axis rejection (void is not an object).");

int main() { return 0; }
