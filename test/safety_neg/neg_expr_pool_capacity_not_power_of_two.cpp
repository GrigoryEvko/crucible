// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// ExprPool::capacity_ is an ExprPool::Capacity, which is
// Refined<power_of_two, size_t>.  The Swiss-table probe masks with the
// capacity minus one, so the one door into the type must refuse a
// capacity that is not a power of two.
//
// Expected diagnostic: the power_of_two precondition of mint_refined
// fails in a constant expression.

#include <crucible/ExprPool.h>
#include <fixy/Refined.h>

#include <cstddef>

int main() {
    constexpr crucible::ExprPool::Capacity bad = ::fixy::mint_refined<::fixy::power_of_two>(std::size_t{12});
    (void)bad;
}
