// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-214 audit fixture: MVRegister canonicalizes concurrent versions
// into byte-stable order.  The value type must therefore be totally
// ordered when equal vector clocks carry different values.

#include <crucible/canopy/Crdt.h>

struct EqualityOnlyPayload {
    int value = 0;

    [[nodiscard]] friend constexpr bool operator==(
        EqualityOnlyPayload const&,
        EqualityOnlyPayload const&) = default;
};

int main() {
    crucible::canopy::MVRegister<EqualityOnlyPayload, 4, 4> reg;
    (void)reg;
    return 0;
}
