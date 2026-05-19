// fixy_neg: direct stance-alias value construction is rejected —
// closes fixy-A4-018 (paired with neg_fixy_fn_direct_ctor_bypass.cpp
// for distinct call-site shapes).
//
// HS14 fixture 2/2.  `fixy::stance::PureLinear<int>` is an alias for
// `fixy::fn<int, all-strict-grants>`.  Pre-A4-018, the alias inherited
// the public value ctor and could be constructed directly —
// `fixy::stance::PureLinear<int>{42}` would silently produce a
// value-carrying binding without going through `mint_fn_for<
// stance::PureLinear>(42)`.  This is the §XXI grep-target leak that
// motivated the private-ctor tightening.
//
// Tightened form: the underlying `fn`'s value ctor is private; the
// stance alias inherits that visibility.  Construction must route
// through `mint_fn_for<UnaryStance>` (or `mint_fn_for<BinaryStance,
// Policy>`), which is the §XXI canonical entry point.
//
// Distinct from neg_fixy_fn_direct_ctor_bypass.cpp: that fixture
// uses the RAW `fixy::fn<T, ...>` spelling; this fixture uses the
// STANCE ALIAS spelling — the form a production engineer is far more
// likely to write and the reason §XXI grep-target completeness matters.
//
// Expected diagnostic: "private" (gcc emits "is private within this
// context" or "declared private here").

#include <crucible/fixy/Fn.h>

namespace fixy = crucible::fixy;

int main() {
    // Direct stance-alias value construction — must reject.  PureLinear
    // is the most common production stance; if this fixture compiles,
    // every production binding can bypass `mint_fn_for` silently.
    auto bad = fixy::stance::PureLinear<int>{42};
    (void)bad;
    return 0;
}
