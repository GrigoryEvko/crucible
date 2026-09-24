// A value spends the stamp that grades it.  An lvalue stamp would stay
// usable after the value takes it, so the next value could take it again.
// Budgeted takes the stamp by rvalue only.

#include <fixy/Budgeted.h>
#include <foundation/effects/Ctx.h>

int main() {
    namespace fe = ::foundation::effects;
    fe::ExecCtx<fe::Init, fe::Row<fe::Effect::Init, fe::Effect::IO>> const init{fe::testing::init()};
    fixy::BudgetAuthority authority = fixy::mint_budget_authority(init);
    fixy::BudgetStamp stamp = authority.grant(fixy::BitsBudgetBound{8}, fixy::PeakBytesBound{64});
    fixy::Budgeted<int> const first{1, stamp};
    return first.peek();
}
