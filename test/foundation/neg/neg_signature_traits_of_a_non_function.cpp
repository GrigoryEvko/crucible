// signature_traits reflects the parameters of a function.  A value that is
// not a pointer to a function has no parameter list, and the reflection
// query in the trait refuses it.  arity_v is the entry that the refusal
// reaches first.

#include <foundation/reflect/Signature.h>

int main() { return static_cast<int>(::foundation::reflect::arity_v<42>); }
