// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// An MV register sorts concurrent versions into one byte-stable order.
// Two versions with equal vector clocks are ordered by value, so the value
// type must be totally ordered.

#include <crucible/canopy/Crdt.h>

struct EqualityOnlyPayload {
    int value = 0;

    [[nodiscard]] friend constexpr bool operator==(EqualityOnlyPayload const&, EqualityOnlyPayload const&) = default;
};

int main() {
    crucible::canopy::MVRegister<EqualityOnlyPayload, 4, 4> reg;
    (void)reg;
    return 0;
}
