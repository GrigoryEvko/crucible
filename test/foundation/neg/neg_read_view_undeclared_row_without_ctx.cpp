// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture 2 of 2 for foundation::permissions::mint_read_view.
//
// The relation from tag to row is closed: a pure region says so with
// Row<>, and a region that says nothing has not been decided.  The gate
// therefore fails closed — permission_row_empty_v answers false for a tag
// that declares no row, so an undeclared region is refused rather than
// admitted by the omission.
//
// The call is written in an unevaluated operand on purpose.  Minting a
// permission for this tag would fire mint_permission_root's own row
// assertion, and the fixture would then prove that assertion rather than
// this gate.  Overload resolution alone is enough to show the refusal.
//
// Distinct mismatch class from
// neg_read_view_effectful_row_without_ctx.cpp (fixture 1): there the row
// is declared and names IO; here no row is declared at all, and the
// question is which way the gate fails when it has nothing to read.
//
// Expected diagnostic: no matching function for mint_read_view, whose
// candidate was discarded because ReadViewNeedsNoCtx is not satisfied.

#include <foundation/permissions/Permission.h>
#include <foundation/permissions/ReadView.h>

#include <utility>

namespace {
// Declares no permission_row, and no edge names it either.
struct UndecidedRegion {};

using UndecidedPermission = ::foundation::permissions::Permission<UndecidedRegion>;
}  // namespace

// The alias is never used.  Naming the result of the refused call is the
// whole fixture, and reading it again would report a second error on a
// second line, which the neg driver counts as a fixture rejecting for
// more than one reason.
using Borrowed = decltype(::foundation::permissions::mint_read_view(std::declval<UndecidedPermission const&>()));

int main() { return 0; }
