// The two grades are two types.  With raw integers, a peak of 4096 bytes
// passed where the bit count belongs would be granted as the bit count,
// and the gate would then compare it against the bit threshold.  A grant
// takes a bits bound and then a peak bound, and nothing converts one into
// the other.

#include <fixy/Budgeted.h>
#include <foundation/effects/Ctx.h>

int main() {
    namespace fe = ::foundation::effects;
    fe::ExecCtx<fe::Init, fe::Row<fe::Effect::Init, fe::Effect::IO>> const init{fe::testing::init()};
    fixy::BudgetAuthority authority = fixy::mint_budget_authority(init);
    fixy::Budgeted<int> const swapped{1, authority.grant(fixy::PeakBytesBound{4096}, fixy::BitsBudgetBound{8})};
    return swapped.peek();
}
