// param_type_t names one parameter by index, and the index is bounded by
// the function's own arity.  Without the bound the splice reads past the
// end of the static array, which is a diagnostic from inside the array
// rather than at the call, and the caller cannot see which index was
// wrong.
//
// VIOLATION: a TU asks a binary function for its third parameter.
//
// Expected diagnostic: the bound constraint on param_type_t fails.

#include <foundation/reflect/Signature.h>

namespace {

void witness_binary(int, double) noexcept {}

// The alias is declared and never used.  Naming it in an expression
// would fail a second time, at the use, and a fixture that rejects at
// two of its own lines cannot say which rejection its regexes witnessed.
using past_the_end = ::foundation::reflect::param_type_t<&witness_binary, 2>;

}  // namespace

int main() { return 0; }
