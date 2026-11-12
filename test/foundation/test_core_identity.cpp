// Tests of foundation/core/Identity.h: mint_identity and Identity.

#include <foundation/core/Identity.h>

#include <foundation/Platform.h>

namespace {

using ::foundation::core::Identity;
using ::foundation::core::mint_identity;

// A copy is equal to its source, and two mints are never equal.
void test_copy_and_fresh() {
    Identity const first = mint_identity();
    Identity const copy = first;
    Identity const second = mint_identity();
    CRUCIBLE_FATAL_INVARIANT(first == copy);
    CRUCIBLE_FATAL_INVARIANT(!(first == second));
    CRUCIBLE_FATAL_INVARIANT(!(copy == second));
}

// The property: in a run of mints, each identity is equal to itself, and to
// neither the first identity nor the identity before it.  An assignment
// keeps the identity of its source.
void test_each_mint_is_fresh() {
    Identity const anchor = mint_identity();
    Identity previous = mint_identity();
    for (int turn = 0; turn < 4096; ++turn) {
        Identity const next = mint_identity();
        CRUCIBLE_FATAL_INVARIANT(next == next);
        CRUCIBLE_FATAL_INVARIANT(!(next == previous));
        CRUCIBLE_FATAL_INVARIANT(!(next == anchor));
        previous = next;
        CRUCIBLE_FATAL_INVARIANT(previous == next);
    }
}

}  // namespace

int main() {
    test_copy_and_fresh();
    test_each_mint_is_fresh();
    return 0;
}
