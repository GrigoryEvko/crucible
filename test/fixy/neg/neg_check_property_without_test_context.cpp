// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// property runs the body of a test and ends the process on a failure, and
// only a test calls it.  A background context owns no Test capability, so
// the concept TestContext refuses it at the call.
//
// Expected diagnostic: property has no viable candidate, and the note names
// the concept.

#include <fixy/check/Property.h>

namespace eff = foundation::effects;

namespace {
using BgDrainCtx = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::Alloc>>;
}  // namespace

int main() {
    BgDrainCtx const ctx{eff::testing::bg()};
    fixy::check::property(ctx, 1, 10, [](fixy::check::Draw&) noexcept { return true; });
    return 0;
}
