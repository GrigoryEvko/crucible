// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-WRAP fixture #9: AppendOnly<WriteOnce<T>> is structurally
// redundant and rejected by a named static_assert.
//
// Violation: AppendOnly<T> already guarantees that emplaced elements
// are never mutated, reassigned, or removed — wrapping inside a
// WriteOnce<T> adds no invariant and doubles per-element storage by
// one std::optional tag byte.  The substrate static_asserts
// `!is_writeonce_v<T>`; routing through fixy::wrap must preserve
// the named diagnostic identically.
//
// Expected diagnostic: substring "AppendOnly<WriteOnce<T>> is
// redundant" / "static assertion failed".

#include <crucible/fixy/Wrap.h>

namespace fw = crucible::fixy::wrap;

struct TypeFixyWrapAppendOnlyRedundant {};

// Should FAIL: AppendOnly<WriteOnce<T>> fires the redundancy
// static_assert in safety::AppendOnly's primary template.
namespace {
using BadStack = fw::AppendOnly<fw::WriteOnce<int>>;
[[maybe_unused]] BadStack obj{};
}  // namespace

int main() {
    return 0;
}
