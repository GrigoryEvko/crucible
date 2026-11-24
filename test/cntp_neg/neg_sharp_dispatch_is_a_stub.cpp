// NEGATIVE-COMPILE TEST.  This file must fail to compile.
//
// No switch reduces, so dispatch_sharp_allreduce is a stub that returns a
// fallback for every request.  Its deprecation makes each call site see that
// at compile time.  The pragma turns the warning into an error whatever the
// build flags are.  The call writes the run of a frame, as each caller does.

#pragma GCC diagnostic error "-Wdeprecated-declarations"

#include <crucible/cntp/_wip/Sharp.h>
#include <fixy/Ctx.h>

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
    auto plan = shp::mint_sharp_fabric_plan(init, sw, caps,
                                            shp::admit_sharp_participant_count(8).expect("8 participants is positive"))
                    .expect("a SHARP switch makes a plan");

    ::foundation::core::FixedArray<float, 1> const input{1.0f};
    ::foundation::core::FixedArray<float, 1> output{};
    const crucible::NumericalRecipe recipe{};
    return output.with_mut([&](auto run) noexcept -> int {
        auto result = shp::dispatch_sharp_allreduce(
            input.view(), std::move(run), recipe, shp::SharpRecipeLaws{.associative = true, .commutative = true}, plan);
        return result.is_ok() ? 0 : 1;
    });
}
