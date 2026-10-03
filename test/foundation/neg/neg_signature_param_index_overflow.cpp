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

// The requirement is the one use of the parameter type.  An alias
// declaration gives a second error at the declaration, and a second use
// of the type gives a second error at the use.  A fixture that rejects at
// two places cannot say which rejection its regexes witnessed.
static_assert(requires { typename ::foundation::reflect::param_type_t<&witness_binary, 2>; });

}  // namespace

int main() { return 0; }
