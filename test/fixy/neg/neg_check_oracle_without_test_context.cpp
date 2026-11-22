// Negative compile test.  This file must fail to compile.
//
// oracle compares a family operation with its model and ends the process on
// a difference, and only a test calls it.  A background context owns no Test
// capability, so the concept TestContext refuses it at the call.
//
// Expected diagnostic: oracle has no viable candidate, and the note names the
// concept.

#include <fixy/check/Property.h>

namespace eff = foundation::effects;

namespace {
using BgDrainCtx = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::Alloc>>;
}  // namespace

int main() {
    BgDrainCtx const ctx{eff::testing::bg()};
    fixy::check::oracle(
        ctx, 1, 10, [](fixy::check::Draw& draw) noexcept { return draw.integer<int>(0, 9); },
        [](int const& value) noexcept { return value; }, [](int const& value) noexcept { return value; });
    return 0;
}
