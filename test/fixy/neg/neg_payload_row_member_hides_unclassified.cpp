// A plain class that holds an unrostered wrapper in a member is refused,
// as the wrapper is at the root.  The class itself is not a
// specialization, so no roster answers for it.  The walk reads the member
// and refuses the wrapper, and the diagnostic names the wrapper, which the
// instantiation note for payload_row<T> does not.
//
// VIOLATION: a TU asks for the row of a class whose member is an
// unrostered wrapper around an engaged computation.

#include <fixy/concurrent/PayloadRow.h>

namespace {

namespace eff = ::foundation::effects;

template <class T>
struct UnclassifiedWrapper {
    using value_type = T;
    T held;
};

struct HoldsUnclassified {
    int tag = 0;
    UnclassifiedWrapper<eff::Computation<eff::Row<eff::Effect::Bg>, int>> held;
};

// Declared and never used, as in the sibling fixtures.
using forged = ::fixy::concurrent::payload_row_t<HoldsUnclassified>;

}  // namespace

int main() { return 0; }
