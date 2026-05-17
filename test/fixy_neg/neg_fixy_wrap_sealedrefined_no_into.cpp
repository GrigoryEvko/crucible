// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-WRAP fixture #3: SealedRefined has no into() extractor.
//
// Violation: SealedRefined deliberately omits `into() &&` — once a
// value is sealed-refined the only way to observe it is via
// `value() const`.  The using-declaration in fixy/Wrap.h must
// preserve the absent member.
//
// Expected diagnostic: substring "no member named 'into'" /
// "has no member" / "'into' not found".

#include <crucible/fixy/Wrap.h>

#include <utility>

namespace fw  = crucible::fixy::wrap;
namespace saf = crucible::safety;

struct TypeFixyWrapSealedRefinedInto {};

int main() {
    fw::SealedRefined<saf::positive, int> sr{1};
    // Should FAIL: SealedRefined deliberately omits .into().
    int extracted = std::move(sr).into();
    (void)extracted;
    return 0;
}
