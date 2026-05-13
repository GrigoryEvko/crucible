// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.

#include <crucible/forge/recipes/Network.h>

namespace net = crucible::forge::recipes;

struct OddParticipantRecipe {
    static constexpr crucible::ReductionDeterminism determinism =
        crucible::ReductionDeterminism::ORDERED;
    static constexpr bool associative = true;
    static constexpr bool commutative = true;
    static constexpr bool participant_count_power_of_two = false;
};

template <class R, class A>
    requires net::NetworkRecipeEligible<R, A>
constexpr bool accepts_network_recipe() {
    return true;
}

static_assert(accepts_network_recipe<
              OddParticipantRecipe,
              net::RecursiveHalvingDoublingAlgorithm>());
