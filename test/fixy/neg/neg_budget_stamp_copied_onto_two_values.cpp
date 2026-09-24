// An allowance is spent one time.  A copy of a stamp would put one grant
// on two payloads, and each would claim the whole grant, so the claims of
// the program would sum to more than its grants.  The stamp does not copy.

#include <fixy/Budgeted.h>
#include <foundation/effects/Ctx.h>

int main() {
    namespace fe = ::foundation::effects;
    fe::ExecCtx<fe::Init, fe::Row<fe::Effect::Init, fe::Effect::IO>> const init{fe::testing::init()};
    fixy::BudgetAuthority authority = fixy::mint_budget_authority(init);
    fixy::BudgetStamp const stamp = authority.grant(fixy::BitsBudgetBound{8}, fixy::PeakBytesBound{64});
    fixy::BudgetStamp copy = stamp;
    fixy::Budgeted<int> const second{2, static_cast<fixy::BudgetStamp&&>(copy)};
    return second.peek();
}
