// An append to an ordered log can allocate.  So it takes the allocation
// capability first, and a tag of a different effect pays for no append.

#include <fixy/Mutation.h>
#include <foundation/effects/Effect.h>

int main() {
    auto log = fixy::mint_ordered_append_only<int>();
    log.append(::foundation::effects::testing::test().io, 1);
    return static_cast<int>(log.size());
}
