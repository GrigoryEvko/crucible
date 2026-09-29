// A payload template on none of the three rosters is refused, and the
// diagnostic names it.
//
// An extractor whose primary template answered Row<> would report
// "carries no effect" for a wrapper that no arm names.  That wrapper
// would then satisfy every execution context.  A fail-open default has
// nothing to fail against.
//
// Here the primary is a static_assert.  The message is fixed text, so
// the type arrives in the instantiation note the compiler prints beside
// it, which is what the second regex matches.
//
// VIOLATION: a TU asks for the row of a wrapper the rosters do not name.
//
// Expected diagnostic: the payload_row_is_classified static_assert, with
// UnclassifiedWrapper<int> in the instantiation note.

#include <fixy/concurrent/PayloadRow.h>

namespace {

// Shaped exactly like a transparent wrapper — it has a value_type, so
// the extractor could have unwrapped it — and absent from the roster.
// Looking like a wrapper is not membership.
template <class T>
struct UnclassifiedWrapper {
    using value_type = T;
};

// Declared and never used.  Naming it in an expression would fail a
// second time, at the use, and a fixture that rejects at two of its own
// lines cannot say which rejection its regexes witnessed.
using forged = ::fixy::concurrent::payload_row_t<UnclassifiedWrapper<int>>;

}  // namespace

int main() { return 0; }
