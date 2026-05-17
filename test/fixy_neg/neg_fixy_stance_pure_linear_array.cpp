// fixy_neg: stance::PureLinear<int[5]> rejects via Type-axis gate.
//
// HS14 floor for FIXY-AUDIT-D4 (PureLinear stance).  PureLinear is
// the all-strict stance with implicit Linear usage.  Instantiating
// it with an array Type fires IsAccepted's type-axis check
// (`type_is_object_or_function<int[5]>` is false — arrays are
// excluded because array-to-pointer decay would corrupt the
// wrapper's round-trip with safety::fn::Fn).
//
// Expected diagnostic: "IsAccepted" — the satisfaction-failure chain
// names the top-level concept (Type-axis rejection happens inside
// IsAccepted's body, not via a named per-axis diagnostic tag).

#include <crucible/fixy/Fn.h>

namespace stance = crucible::fixy::stance;

// Direct stance instantiation: class-body static_assert fires on
// completion, forced by sizeof.
using BadPureLinearArray = stance::PureLinear<int[5]>;

static_assert(sizeof(BadPureLinearArray) > 0,
    "instantiate stance::PureLinear<int[5]> to force its class-body "
    "static_assert (Type-axis gate must reject arrays).");

int main() { return 0; }
