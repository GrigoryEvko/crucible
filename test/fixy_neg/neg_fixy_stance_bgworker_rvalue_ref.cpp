// fixy_neg: stance::BgWorker<int&&> rejects via Type-axis gate.
//
// HS14 floor for FIXY-AUDIT-D4 (BgWorker stance).  BgWorker pins
// Effect={Bg, Alloc} but otherwise all-strict.  An rvalue-reference
// Type is not an object type and fails IsAccepted's type-axis check.
//
// Distinct from lvalue-reference (covered by other stances): rvalue-
// ref types have additional move-binding semantics that would still
// be invalid here, but the rejection class is the same overall (not-
// an-object).  The fixture documents that BgWorker exercises the
// same gate.
//
// Expected diagnostic: "IsAccepted".

#include <crucible/fixy/Fn.h>

namespace stance = crucible::fixy::stance;

using BadBgRvalueRef = stance::BgWorker<int&&>;

static_assert(sizeof(BadBgRvalueRef) > 0,
    "instantiate stance::BgWorker<int&&> to force the Type-axis "
    "rejection (rvalue references are not object types).");

int main() { return 0; }
