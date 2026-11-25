// A growth of a log ends each reference to an element.  So the subscript of a
// log gives the element by value, and no reference that writes binds to it.

#include <fixy/Mutation.h>
#include <foundation/effects/Effect.h>

int main() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    auto log = fixy::mint_append_only<int>();
    log.append(alloc, 1);
    int& first = log[0];
    log.append(alloc, 2);
    return first;
}
