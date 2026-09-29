// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// ExprPool::make returns DetSafe<Pure, Tagged<const Expr*,
// source::Interned>>.  A value at the same tier that carries a different
// source tag must not take the place of source::Interned.
//
// Expected diagnostic: no conversion from ExternalExpr to PureInternedExpr.

#include <crucible/ExprPool.h>
#include <fixy/Bands.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>

using ExternalTagged = ::fixy::Tagged<const crucible::Expr*, ::fixy::tags::source::External>;
using ExternalExpr = ::fixy::det_safe::Pure<ExternalTagged>;

static void consume(crucible::ExprPool::PureInternedExpr) {}

int main() {
    const crucible::Expr* const no_expr = nullptr;
    ExternalExpr external =
        ::fixy::mint_band<ExternalExpr>(::fixy::mint_tagged<::fixy::tags::source::External>(no_expr));
    consume(external);
    return 0;
}
