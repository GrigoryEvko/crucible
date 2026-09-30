// NEGATIVE-COMPILE TEST.  This file must fail to compile.
//
// No switch reduces, so dispatch_sharp_allreduce is a stub that returns a
// fallback for every request.  Its deprecation makes each call site see that
// at compile time.  The pragma turns the warning into an error whatever the
// build flags are.

#pragma GCC diagnostic error "-Wdeprecated-declarations"

#include <crucible/cntp/_wip/Sharp.h>

#include <expected>
#include <span>

namespace shp = crucible::cntp::_wip::sharp;

namespace {

[[maybe_unused]] std::expected<shp::DeclaredSharpDispatch, shp::SharpError>
dispatch_once(std::span<const float> input, std::span<float> output, crucible::NumericalRecipe const& recipe,
              shp::SharpRecipeLaws laws, shp::DeclaredSharpFabricPlan plan) noexcept {
    return shp::dispatch_sharp_allreduce(input, output, recipe, laws, plan);
}

}  // namespace

int main() { return 0; }
