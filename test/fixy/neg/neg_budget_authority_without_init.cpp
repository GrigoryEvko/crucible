// A budget authority is minted only by process startup.  A test context
// owns no Init, so a producer cannot make an authority that grants it a
// zero budget.

#include <fixy/Budgeted.h>
#include <foundation/effects/Ctx.h>

int main() {
    namespace fe = ::foundation::effects;
    fe::ExecCtx<fe::Test, fe::Row<fe::Effect::Test, fe::Effect::IO>> const scope{fe::testing::test()};
    fixy::BudgetAuthority authority = fixy::mint_budget_authority(scope);
    return static_cast<int>(authority.grant(fixy::BitsBudgetBound{0}, fixy::PeakBytesBound{0}).bits().raw());
}
