// fixy_neg: stance::IoFunction<volatile int> rejects via Type-axis gate.
//
// HS14 floor for FIXY-AUDIT-D4 (IoFunction stance, distinct angle).
// Top-level volatile fails IsAccepted: volatile Types break the
// wrapper's NSDMI + move semantics.  Distinct from the const fixture:
// const semantics vs volatile-rebinding semantics.
//
// Expected diagnostic: "IsAccepted".

#include <crucible/fixy/Fn.h>

namespace stance = crucible::fixy::stance;

using BadIoVolatile = stance::IoFunction<volatile int>;

static_assert(sizeof(BadIoVolatile) > 0,
    "instantiate stance::IoFunction<volatile int> to force the "
    "Type-axis rejection (top-level volatile is not movable).");

int main() { return 0; }
