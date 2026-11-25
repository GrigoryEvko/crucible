// A loop over a list that writes the element could end the storage of an
// element that a View of the caller names.  The loop lends const elements, so
// a body that writes the elements takes the list with with_mut.

#include <foundation/core/Array.h>
#include <foundation/core/List.h>

namespace fc = ::foundation::core;

int main() {
    fc::FixedList<fc::HeapArray<int>, 2> runs{};
    for (fc::HeapArray<int>& run : runs)
        run = fc::HeapArray<int>{};
    return 0;
}
