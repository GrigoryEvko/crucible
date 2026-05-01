// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// P1-AUDIT-4 (#855): CapMatchesCtx rejects when the Cap's effect is
// not in the Ctx's row.
//
// Violation: a Capability<Effect::IO, Bg> arrives at a function
// that requires CapMatchesCtx<Cap, BgDrainCtx>.  BgDrainCtx's row
// is Row<Bg, Alloc> — IO is NOT in it, so the constraint fails.
//
// Expected diagnostic: "associated constraints are not satisfied"
// pointing at CapMatchesCtx.

#include <crucible/effects/Capability.h>

namespace eff = crucible::effects;

template <eff::IsCapability Cap, eff::IsExecCtx Ctx>
    requires eff::CapMatchesCtx<Cap, Ctx>
constexpr void use_cap_in_ctx(Cap&&, Ctx const&) noexcept {}

int main() {
    eff::Bg bg;
    auto io_cap = eff::mint_cap<eff::Effect::IO>(bg);
    eff::BgDrainCtx drain_ctx;          // row = {Bg, Alloc} — no IO
    use_cap_in_ctx(std::move(io_cap), drain_ctx);  // CapMatchesCtx fails
    return 0;
}
