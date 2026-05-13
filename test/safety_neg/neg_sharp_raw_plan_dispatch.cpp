// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// HS14 fixture for GAPS-133. SHARP dispatch requires a
// source::Sharp-declared fabric plan, not a raw aggregate.

#include <crucible/cntp/Sharp.h>

#include <array>

namespace shp = crucible::cntp::sharp;

int main() {
    std::array<float, 1> input{1.0f};
    std::array<float, 1> output{};
    crucible::NumericalRecipe recipe{};
    shp::SharpFabricPlan plan{};
    auto result = shp::dispatch_sharp_allreduce(
        input, output, recipe,
        shp::SharpRecipeLaws{.associative = true, .commutative = true},
        plan);
    return result.has_value() ? 0 : 1;
}
