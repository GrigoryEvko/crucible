// A budget authority is minted only by process startup.  A background
// context owns no Init, so a background producer cannot make an authority
// that grants it any budget it likes.

#include <fixy/Budgeted.h>
#include <foundation/effects/Ctx.h>

int main() {
    namespace fe = ::foundation::effects;
    fe::ExecCtx<fe::Bg, fe::Row<fe::Effect::Bg, fe::Effect::Alloc, fe::Effect::IO>> const background{fe::testing::bg()};
    fixy::BudgetAuthority authority = fixy::mint_budget_authority(background);
    return static_cast<int>(authority.grant(fixy::BitsBudgetBound{0}, fixy::PeakBytesBound{0}).bits().raw());
}
