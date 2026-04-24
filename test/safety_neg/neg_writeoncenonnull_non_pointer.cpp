// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: WriteOnceNonNull<T> instantiated with a non-pointer T.
// The primitive's whole value proposition is the nullptr-sentinel —
// sizeof(T*) storage and nullptr as the unset marker.  That only makes
// sense for pointer types.  Per #77 the primary template's
// `[WriteOnceNonNull_NonPointer_Type]` static_assert fires at the
// instantiation site, redirecting callers to WriteOnce<T> (which
// carries a std::optional tag and handles non-pointer single-set).

#include <crucible/safety/Mutation.h>

using crucible::safety::WriteOnceNonNull;

// Intent: the caller reached for WriteOnceNonNull but accidentally
// passed the pointee type instead of the pointer type.
// Should be: WriteOnceNonNull<int*> — pointer slot.
// Is:        WriteOnceNonNull<int>  — non-pointer, rejected.
WriteOnceNonNull<int> g_slot;

int main() { return 0; }
