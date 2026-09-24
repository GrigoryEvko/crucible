// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Fixture 2 of 2 for the row gate of foundation::permissions::with_read_view.
//
// A tag that declares no row says nothing about who may hold its region.
// permission_row_empty_v answers false for it, so the gate refuses the
// tag and does not admit it by omission.
//
// A different class from neg_read_view_effectful_row_without_ctx.cpp:
// there the row is declared and names an effect.
//
// Expected diagnostic: no matching function for with_read_view, whose
// candidate was discarded because ReadViewNeedsNoCtx is not satisfied.

#include <foundation/permissions/Permission.h>
#include <foundation/permissions/ReadView.h>

#include <utility>

namespace {

struct UndecidedRegion {};

using UndecidedPermission = ::foundation::permissions::Permission<UndecidedRegion>;

}  // namespace

using Lent = decltype(::foundation::permissions::with_read_view(std::declval<UndecidedPermission&&>(),
                                                                [](auto const&) noexcept {}));

int main() { return 0; }
