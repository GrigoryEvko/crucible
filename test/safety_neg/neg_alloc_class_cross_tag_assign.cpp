// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: assigning AllocClass<TAG_A, T> to AllocClass<TAG_B, T>
// when TAG_A != TAG_B.
//
// Different Tag template arguments produce DIFFERENT class
// instantiations.  No converting assignment, no implicit conversion.
//
// Concrete bug-class this catches: a refactor adding a templated
// converting-assign operator on AllocClass would let a Heap-tier
// value silently flow into a Stack-tier slot.
//
// [GCC-WRAPPER-TEXT] — assignment-operator type-mismatch rejection.

#include <crucible/safety/AllocClass.h>

using namespace crucible::safety;

int main() {
    AllocClass<AllocClassTag_v::Stack, int> stack_value{42};
    AllocClass<AllocClassTag_v::Heap,  int> heap_value{7};

    // Should FAIL: stack_value and heap_value are DIFFERENT types.
    stack_value = heap_value;
    return stack_value.peek();
}
