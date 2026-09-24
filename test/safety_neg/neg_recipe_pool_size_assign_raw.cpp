// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// RecipePool::size_ is a RecipePool::Size, which is Monotonic<uint32_t>.
// The only mutations are bump() and advance(), and each one keeps the
// count from going back.  An assignment from a raw integer would skip
// that check, so the type must refuse it.
//
// Expected diagnostic: no assignment operator takes a raw uint32_t.

#include <crucible/RecipePool.h>
#include <fixy/Mutation.h>

#include <cstdint>

int main() {
    crucible::RecipePool::Size count = ::fixy::mint_monotonic<std::uint32_t>(0u);
    count = 1u;
    return 0;
}
