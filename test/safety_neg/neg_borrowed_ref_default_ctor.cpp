// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #1 of 2 for safety::Borrowed (#1080).
//
// Premise: BorrowedRef<T>{} (default construction) MUST be a compile
// error.  The wrapper's "always-bound, never null" contract is the
// load-bearing safety property — every dereference assumes the
// pointer is live and non-null.  If a default ctor existed, it would
// have to either:
//   (a) leave ptr_ uninitialized — InitSafe violation
//   (b) set ptr_ = nullptr — admit a null sentinel, defeating the
//       always-bound contract and forcing every dereference to
//       null-check (the exact pattern the wrapper exists to prevent)
//
// By DELETING the default ctor at the API surface, the wrapper makes
// "BorrowedRef<T> r;" or "BorrowedRef<T>{}" a compile error at every
// would-be construction site.  Callers who genuinely need
// "may-be-null borrow" semantics must wrap explicitly:
// `Optional<BorrowedRef<T>>` — which makes the null state visible at
// every read.
//
// Expected diagnostic: "use of deleted function" / "deleted" /
// "default constructor" pointing at the BorrowedRef<int>{} call site.

#include <crucible/safety/Borrowed.h>

namespace saf = crucible::safety;

int main() {
    // Bridge fires: BorrowedRef<int> has the default constructor
    // explicitly = delete, so this must produce a deleted-function
    // diagnostic at the construction site.
    saf::BorrowedRef<int> bad{};
    (void)bad;
    return 0;
}
