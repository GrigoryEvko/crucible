// NEGATIVE-COMPILE TEST.  This file must fail to compile.
//
// A SHARP dispatch takes a declared fabric plan, and mint_sharp_fabric_plan
// is the one function that makes one.  A plan built by hand does not
// convert.

#include <crucible/cntp/_wip/Sharp.h>

#include <array>

namespace shp = crucible::cntp::_wip::sharp;

int main() {
    std::array<float, 1> input{1.0f};
    std::array<float, 1> output{};
    const crucible::NumericalRecipe recipe{};
    const shp::SharpFabricPlan plan{
        .fabric_switch = {},
        .participant_count = *shp::admit_sharp_participant_count(8),
    };
    auto result = shp::dispatch_sharp_allreduce(input, output, recipe,
                                                shp::SharpRecipeLaws{.associative = true, .commutative = true}, plan);
    return result.has_value() ? 0 : 1;
}
