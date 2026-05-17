// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-WRAP fixture #4: Tagged<T, FromUser> cannot pass where
// Tagged<T, Sanitized> is expected.
//
// Violation: phantom Tag is the load-bearing trust marker; the
// type system rejects untagged → sanitized conversions silently.
// Routing through `fixy::wrap::Tagged` must preserve the strict
// type-identity discipline.
//
// Expected diagnostic: "cannot convert" / "no matching function"
// / "invalid conversion".

#include <crucible/fixy/Wrap.h>

namespace fw  = crucible::fixy::wrap;
namespace saf = crucible::safety;

struct TypeFixyWrapTaggedMismatch {};

namespace {
void only_sanitized(fw::Tagged<int, saf::source::Sanitized>) noexcept {}
}  // namespace

int main() {
    fw::Tagged<int, saf::source::FromUser> raw{42};
    // Should FAIL: FromUser is not Sanitized — implicit retag forbidden.
    only_sanitized(raw);
    return 0;
}
