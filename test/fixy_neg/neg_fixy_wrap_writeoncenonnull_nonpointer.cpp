// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-WRAP fixture #7: WriteOnceNonNull<int> (non-pointer)
// rejects at static_assert.
//
// Violation: WriteOnceNonNull<T> static_asserts std::is_pointer_v<T>
// at the primary template — instantiating with a non-pointer fires
// the named diagnostic.  Routing through `fixy::wrap` must
// preserve the static_assert identically.
//
// Expected diagnostic: substring "WriteOnceNonNull_NonPointer_Type"
// / "requires a pointer type" / "static assertion failed".

#include <crucible/fixy/Wrap.h>

namespace fw = crucible::fixy::wrap;

struct TypeFixyWrapNonNullNonPointer {};

// Should FAIL: instantiating with `int` (non-pointer) fires the
// named static_assert.
namespace {
using NotAllowed = fw::WriteOnceNonNull<int>;
[[maybe_unused]] NotAllowed obj{};
}  // namespace

int main() {
    return 0;
}
