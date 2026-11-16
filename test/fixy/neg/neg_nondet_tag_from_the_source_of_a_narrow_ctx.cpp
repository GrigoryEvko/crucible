// The bare tag of Nondet comes only from a context whose row holds Nondet.
// The background source holds the tag as a public field, but a context lends
// its source only when its row claims each atom of the source.  The drain
// context holds Bg and Alloc, so cap() has no overload there, and the field
// of the source is out of reach.
//
// Expected diagnostic: no candidate of cap, with a note on the Subrow
// constraint.

#include <fixy/Ctx.h>
#include <foundation/effects/Effect.h>

namespace {

[[maybe_unused]] void attempt(fixy::BgDrainCtx const& drain) {
    [[maybe_unused]] foundation::effects::cap::Nondet const tag = drain.cap().nondet;
}

}  // namespace

int main() { return 0; }
