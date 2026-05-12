// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// WRAP-ExprPool-5 (#919): ExprPool::make pins interned provenance.
// A Pure DetSafe value carrying a different source tag still cannot
// substitute for source::Interned.
//
// Expected diagnostic: no conversion from ExternalExpr to PureInternedExpr.

#include <crucible/ExprPool.h>

namespace saf = crucible::safety;

using ExternalExpr = saf::det_safe::Pure<
    saf::Tagged<const crucible::Expr*, saf::source::External>>;

static void consume(crucible::ExprPool::PureInternedExpr) {}

int main() {
  ExternalExpr external{
      saf::Tagged<const crucible::Expr*, saf::source::External>{nullptr}};
  consume(external);
  return 0;
}
