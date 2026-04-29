// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: swap()-ing AllocClass<TAG_A, T> with AllocClass<TAG_B, T>
// when TAG_A != TAG_B.
//
// swap() takes a reference to the SAME class — a member taking
// `AllocClass<Tag, T>&`.  Cross-tag swap is rejected at overload
// resolution.
//
// [GCC-WRAPPER-TEXT] — swap parameter-type mismatch.

#include <crucible/safety/AllocClass.h>
#include <utility>

using namespace crucible::safety;

int main() {
    AllocClass<AllocClassTag_v::Stack, int> stack_value{42};
    AllocClass<AllocClassTag_v::Heap,  int> heap_value{7};

    // Should FAIL: AllocClass<Stack, int>::swap takes
    // AllocClass<Stack, int>&; heap_value is a different type.
    stack_value.swap(heap_value);

    using std::swap;
    swap(stack_value, heap_value);

    return stack_value.peek();
}
