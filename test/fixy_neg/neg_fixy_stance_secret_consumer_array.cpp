// fixy_neg: stance::SecretConsumer<int[3], Policy> rejects via Type-axis gate.
//
// HS14 floor for FIXY-AUDIT-D4 (SecretConsumer stance).  SecretConsumer
// is the canonical declassification stance — pins SecLevel::Public via
// `declassify<Policy>` to discharge §30.14 implicit-flow.  The Policy
// template parameter is captured for audit-trail provenance.
//
// Bad Type (array) fails IsAccepted's type-axis check before the
// declassify-Policy logic runs, so the rejection chain names
// IsAccepted as the offending concept.
//
// Expected diagnostic: "IsAccepted".

#include <crucible/fixy/Fn.h>
#include <crucible/safety/Secret.h>

namespace stance = crucible::fixy::stance;
namespace policy = crucible::safety::secret_policy;

using BadSecretArray = stance::SecretConsumer<int[3], policy::AuditedLogging>;

static_assert(sizeof(BadSecretArray) > 0,
    "instantiate stance::SecretConsumer<int[3], AuditedLogging> to "
    "force the Type-axis rejection (arrays decay).");

int main() { return 0; }
