// NEGATIVE-COMPILE TEST.  This file must fail to compile.
//
// A SHARP dispatch takes a declared fabric plan, and mint_sharp_fabric_plan
// is the one function that makes one.  A plan built by hand does not
// convert.

#include <crucible/cntp/_wip/Sharp.h>

// The dispatch path is a stub, and its deprecation is not the error under
// test.
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

namespace shp = crucible::cntp::_wip::sharp;

int main() {
    ::foundation::core::FixedArray<float, 1> const input{1.0f};
    ::foundation::core::FixedArray<float, 1> output{};
    const crucible::NumericalRecipe recipe{};
    const shp::SharpFabricPlan plan{
        .fabric_switch = {},
        .participant_count = shp::admit_sharp_participant_count(8).expect("8 participants is positive"),
    };
    auto result = shp::dispatch_sharp_allreduce(input.view(), output.view(), recipe,
                                                shp::SharpRecipeLaws{.associative = true, .commutative = true}, plan);
    return result.is_ok() ? 0 : 1;
}
