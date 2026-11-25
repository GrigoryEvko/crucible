// A log never changes an element.  It gives the list of its elements as a const
// list, and each Ref of that list names a const element.

#include <fixy/Mutation.h>
#include <foundation/effects/Effect.h>

int main() {
    auto log = fixy::mint_append_only<int>();
    log.append(::foundation::effects::testing::test().alloc, 1);
    *log.peek().get(0).expect("one element") = 2;
    return log[0];
}
