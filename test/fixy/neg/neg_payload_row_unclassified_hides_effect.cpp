// The refusal does not depend on what the unclassified wrapper hides.
//
// Sibling of neg_payload_row_unclassified_wrapper.cpp, and the half that
// shows what a fail-open default would cost.  There the wrapper holds an
// int, so answering Row<> would be the right answer for the wrong
// reason.  Here it holds a computation engaged at Row<Bg>, so answering
// Row<> would say "this payload needs no capability" about a payload
// that carries a background effect, and a foreground context would
// admit it.
//
// Both fixtures are required.  A roster check that looked at the
// wrapper's contents rather than at its family would pass the first and
// fail the second, and the property is that neither is admitted: the
// refusal is for saying nothing about what it hides.
//
// VIOLATION: a TU asks for the row of an unrostered wrapper around an
// engaged computation.
//
// Expected diagnostic: the payload_row_is_classified static_assert, with
// the wrapper over the Bg-engaged Computation in the instantiation note.

#include <fixy/concurrent/PayloadRow.h>

namespace {

namespace eff = ::foundation::effects;

template <class T>
struct UnclassifiedWrapper {
    using value_type = T;
};

using BgEngaged = eff::Computation<eff::Row<eff::Effect::Bg>, int>;

// Declared and never used, for the reason given in the sibling fixture.
using forged = ::fixy::concurrent::payload_row_t<UnclassifiedWrapper<BgEngaged>>;

}  // namespace

int main() { return 0; }
