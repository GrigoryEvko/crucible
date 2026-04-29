// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: comparing AllocClass<TAG_A, T> with AllocClass<TAG_B, T>
// via operator==.
//
// The friend operator== is a non-template friend declared inside
// the class template.  Each (Tag, T) instantiation has its OWN
// friend taking two AllocClass<Tag, T>&.  Cross-tag comparison
// fails to find a viable operator==.
//
// [GCC-WRAPPER-TEXT] — operator== overload-resolution rejection.

#include <crucible/safety/AllocClass.h>

using namespace crucible::safety;

int main() {
    AllocClass<AllocClassTag_v::Stack, int> stack_value{42};
    AllocClass<AllocClassTag_v::Heap,  int> heap_value{42};

    // Should FAIL: operator== for AllocClass<Stack, int> takes two
    // AllocClass<Stack, int>&; heap_value is AllocClass<Heap, int>.
    return static_cast<int>(stack_value == heap_value);
}
