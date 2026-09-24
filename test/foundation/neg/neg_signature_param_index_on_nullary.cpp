// A nullary function has no parameter to name, so every index is out of
// range.  This is the boundary case of the bound on param_type_t: arity
// is zero, so the constraint `I < arity` cannot hold for any I, and the
// first parameter is as unreachable as the hundredth.
//
// Sibling of neg_signature_param_index_overflow.cpp, which asks a
// function with parameters for one it does not have.  Both are required:
// an off-by-one in the bound would admit index 0 here while still
// rejecting index 2 there.
//
// VIOLATION: a TU asks a nullary function for its first parameter.
//
// Expected diagnostic: the bound constraint on param_type_t fails.

#include <foundation/reflect/Signature.h>

namespace {

void witness_nullary() noexcept {}

// Declared and never used, for the reason given in the sibling fixture.
using no_such_param = ::foundation::reflect::param_type_t<&witness_nullary, 0>;

}  // namespace

int main() { return 0; }
