// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A reference type has no pointer to form, so PublishOnce<T&> cannot
// build its atomic<T*> at all.  The guard rejects it by name here
// rather than leaving the caller a diagnostic about forming a pointer
// to a reference type inside <atomic>, which names neither this header
// nor the mistake.

#include <crucible/handles/PublishOnce.h>

struct CompiledKernel;

int main() {
    crucible::safety::PublishOnce<CompiledKernel&> slot;
    (void)slot;
    return 0;
}
