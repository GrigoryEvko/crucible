// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// ExprPool::intern_count_ is an ExprPool::InternCount, which is
// Monotonic<size_t>.  The only mutations are bump() and advance(), and
// each one keeps the count from going back.  An assignment from a raw
// size_t would skip that check, so the type must refuse it.
//
// Expected diagnostic: no assignment operator takes a raw size_t.

#include <crucible/ExprPool.h>
#include <fixy/Mutation.h>

#include <cstddef>

int main() {
    crucible::ExprPool::InternCount count = ::fixy::mint_monotonic<std::size_t>(0);
    count = std::size_t{1};
    return 0;
}
