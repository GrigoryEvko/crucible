// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.

#include <crucible/forge/_wip/Phases/Comm.h>

namespace phase = crucible::forge::_wip::phases::comm;

struct StrictRecipe {
    static constexpr crucible::ReductionDeterminism determinism =
        crucible::ReductionDeterminism::BITEXACT_STRICT;
    static constexpr bool associative = true;
    static constexpr bool commutative = true;
    static constexpr bool participant_count_power_of_two = true;
};

template <class Recipe>
    requires phase::CommFusionRecipeAllowed<
        Recipe, phase::CommFusionPattern::CompressBeforeSend>
constexpr bool accepts_compress_before_send() {
    return true;
}

static_assert(accepts_compress_before_send<StrictRecipe>());
