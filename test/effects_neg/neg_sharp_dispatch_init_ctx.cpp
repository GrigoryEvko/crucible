// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// HS14 fixture for GAPS-133. SHARP all-reduce dispatch is Bg-row work;
// Init-only contexts can mint the fabric handle but cannot submit work.

#include <crucible/cntp/Sharp.h>

#include <array>

namespace cog = crucible::cog;
namespace eff = crucible::effects;
namespace shp = crucible::cntp::sharp;

int main() {
    cog::CogIdentity sw{};
    sw.uuid = cog::Uuid{1, 2};
    sw.kind = cog::CogKind::NvSwitch;
    cog::NvSwitchTargetCaps caps{};
    caps.features.set(cog::SwitchFeature::Sharp);
    auto plan = shp::mint_sharp_fabric_plan(
        eff::ColdInitCtx{}, sw, caps,
        *shp::admit_sharp_participant_count(8), true, true);
    auto context = shp::mint_sharp_context(eff::ColdInitCtx{}, *plan);

    std::array<float, 1> input{1.0f};
    std::array<float, 1> output{};
    crucible::NumericalRecipe recipe{};
    shp::SharpReducer reducer{std::move(*context)};
    auto result = reducer.allreduce_via_sharp(
        eff::ColdInitCtx{}, input, output, recipe,
        shp::SharpRecipeLaws{.associative = true, .commutative = true},
        *plan);
    return result.has_value() ? 0 : 1;
}
