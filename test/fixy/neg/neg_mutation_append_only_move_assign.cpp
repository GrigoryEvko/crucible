// An assignment from a fresh mint would drop every element of the log.
// AppendOnly deletes both assignments, so the move assignment below names
// a deleted function.

#include <fixy/Mutation.h>
#include <foundation/effects/Effect.h>

int main() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    auto log = fixy::mint_append_only<int>();
    log.append(alloc, 1);
    log.append(alloc, 2);
    log = fixy::mint_append_only<int>();
    return static_cast<int>(log.size());
}
