// A log never changes an element.  Its subscript gives a const element, so no
// reference that writes binds to it.

#include <fixy/Mutation.h>
#include <foundation/effects/Effect.h>

int main() {
    auto log = fixy::mint_append_only<int>();
    log.append(::foundation::effects::testing::test().alloc, 1);
    int& first = log[0];
    first = 2;
    return first;
}
