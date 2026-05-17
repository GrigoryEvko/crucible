// fixy_neg: stance::PureCopy<void(int)> rejects via Type-axis gate.
//
// HS14 floor for FIXY-AUDIT-D4 (PureCopy stance, distinct angle).
// A bare function type fails IsAccepted: function types are not
// trivially-constructible value-semantic types and would corrupt
// the wrapper's NSDMI.  Use a function pointer or callable struct
// instead (per the substrate's Fn diagnostic).
//
// Distinct rejection class from the reference fixture: function
// type vs reference type.
//
// Expected diagnostic: "IsAccepted".

#include <crucible/fixy/Fn.h>

namespace stance = crucible::fixy::stance;

using BadPureCopyFn = stance::PureCopy<void(int)>;

static_assert(sizeof(BadPureCopyFn) > 0,
    "instantiate stance::PureCopy<void(int)> to force the Type-axis "
    "rejection (function types are not value-semantic).");

int main() { return 0; }
