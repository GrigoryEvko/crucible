// A copy assignment from a shorter log would drop elements.
// OrderedAppendOnly deletes both assignments, so the copy assignment
// below names a deleted function.

#include <fixy/Mutation.h>
#include <foundation/effects/Effect.h>

int main() {
    auto shorter = fixy::mint_ordered_append_only<int>();
    auto log = fixy::mint_ordered_append_only<int>();
    log.append(::foundation::effects::testing::test().alloc, 5);
    log = shorter;
    return static_cast<int>(log.size());
}
