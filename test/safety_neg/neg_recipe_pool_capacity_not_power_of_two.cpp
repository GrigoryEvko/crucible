// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// RecipePool::capacity_ is a RecipePool::Capacity, which is
// Refined<power_of_two, uint32_t>.  The probe masks with the capacity
// minus one, so the one door into the type must refuse a capacity that
// is not a power of two.
//
// Expected diagnostic: the power_of_two precondition of mint_refined
// fails in a constant expression.

#include <crucible/RecipePool.h>
#include <fixy/Refined.h>

#include <cstdint>

int main() {
    constexpr crucible::RecipePool::Capacity bad = ::fixy::mint_refined<::fixy::power_of_two>(std::uint32_t{12});
    (void)bad;
}
