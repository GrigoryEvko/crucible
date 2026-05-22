// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-100 HS14 fixture 1/2.  Bridge.h's `lift_syscall_grant_row_t<G>`
// is a concept-gated alias template requiring `IsSyscallGrantTag<G>`.
// Passing a NON-grant type (here: plain `int`) must be rejected at the
// concept's IsGrantTag arm (the structural gate that requires G is
// `final + grant_base + cv-ref-free`).
//
// Architectural intent: the concept gate is the load-bearing diagnostic
// surface.  A consumer writing `lift_syscall_grant_row_t<MyType>` must
// receive a clean "MyType is not a syscall grant tag" message — not a
// cryptic "undefined family_tier" instantiation error.  This fixture
// witnesses the IsSyscallGrantTag concept FIRES first.
//
// Mismatch class for HS14 audit: non-grant type rejected at the
// structural IsGrantTag arm of IsSyscallGrantTag (distinct from
// fixture #2's off-axis-grant rejection at the SyscallSurface arm).
// Both class paths protect the same downstream lift but at orthogonal
// stages of the concept's conjunction.
//
// Expected diagnostic: GCC concept-failure message mentioning
// `IsSyscallGrantTag` OR `IsGrantTag` OR `lift_syscall_grant_row_t`
// requires-clause failure; the regex below matches either the named
// concept or the alias-template-id.

#include <crucible/fixy/syscall/Bridge.h>

namespace cb = crucible::fixy::syscall::bridge;

// Passing `int` — a non-grant type — to the concept-gated lift alias.
// The concept fails at its IsGrantTag clause; the alias requires-
// clause emits a named-concept satisfaction-failure diagnostic.
using Bad = cb::lift_syscall_grant_row_t<int /* not a grant tag */>;

int main() {
    [[maybe_unused]] Bad* b = nullptr;
    return 0;
}
