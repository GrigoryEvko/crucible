// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// WRAP-ExprPool-5 (#919): ExprPool::make returns
// DetSafe<Pure, Tagged<const Expr*, source::Interned>>.  A raw Expr*
// must not satisfy a consumer that requires interned provenance.
//
// Expected diagnostic: no conversion from const Expr* to PureInternedExpr.

#include <crucible/ir001/ExprPool.h>

static void consume(crucible::ExprPool::PureInternedExpr) {}

int main() {
  const crucible::Expr* raw = nullptr;
  consume(raw);
  return 0;
}
