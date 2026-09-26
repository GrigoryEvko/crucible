// NEGATIVE-COMPILE TEST.  This file must fail to compile.
//
// A SHARP all-reduce is background work.  The foreground context claims no
// effect, so the dispatch refuses it.

#include <crucible/cntp/_wip/Sharp.h>
#include <fixy/Ctx.h>

#include <array>
#include <utility>

namespace cog = crucible::cog;
namespace shp = crucible::cntp::_wip::sharp;

int main() {
    cog::CogIdentity sw{};
    sw.uuid = cog::Uuid{1, 2};
    sw.kind = cog::CogKind::NvSwitch;
    cog::NvSwitchTargetCaps caps{};
    caps.features.set(cog::SwitchFeature::Sharp);

    ::fixy::ColdInitCtx init{::foundation::effects::testing::init()};
    auto plan = shp::mint_sharp_fabric_plan(init, sw, caps, *shp::admit_sharp_participant_count(8), true, true);
    auto context = shp::mint_sharp_context(init, *plan);

    std::array<float, 1> input{1.0f};
    std::array<float, 1> output{};
    const crucible::NumericalRecipe recipe{};
    shp::SharpReducer reducer{std::move(*context)};
    const ::fixy::HotFgCtx hot = ::foundation::effects::testing::foreground();
    auto result = reducer.allreduce_via_sharp(hot, input, output, recipe,
                                              shp::SharpRecipeLaws{.associative = true, .commutative = true}, *plan);
    return result.has_value() ? 0 : 1;
}
