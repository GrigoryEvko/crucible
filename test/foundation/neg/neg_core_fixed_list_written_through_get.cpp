// get() lends the element for a read, also on a list that is not const, so a
// Ref of it writes no element that a second name of the list holds.

#include <foundation/core/Array.h>
#include <foundation/core/List.h>

namespace fc = ::foundation::core;

int main() {
    fc::FixedList<fc::HeapArray<int>, 2> runs{};
    static_cast<void>(runs.append(fc::HeapArray<int>{}).expect("the list has room"));
    *runs.get(0).expect("the list holds an element") = fc::HeapArray<int>{};
    return 0;
}
