// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture 1 of 2 for foundation::permissions::mint_read_loan.
//
// A read loan opens views through with_read_view, so the loan has the row
// gate of the door.  A loan of a region whose row names IO would carry a
// read proof of that region to a scope that declares no context.
//
// A different class from neg_read_loan_from_named_token.cpp (fixture 2):
// there the region is pure and the token is named.
//
// Expected diagnostic: no matching function for mint_read_loan, whose
// candidate was discarded because ReadViewNeedsNoCtx is not satisfied.

#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>
#include <foundation/permissions/ReadView.h>

#include <utility>

namespace {

struct MappedRegion {
    using permission_row = ::foundation::effects::Row<::foundation::effects::Effect::IO>;
};

using IoCtx = ::foundation::effects::detail::ctx_witnesses::BgIoWitness;

}  // namespace

int main() {
    auto owned =
        ::foundation::permissions::mint_permission_root<MappedRegion>(IoCtx{::foundation::effects::testing::bg()});
    [[maybe_unused]] auto loan = ::foundation::permissions::mint_read_loan(std::move(owned));
    return 0;
}
