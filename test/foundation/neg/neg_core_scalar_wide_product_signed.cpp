// A WideProduct holds the halves of an unsigned product: a WideProduct of
// a signed type does not compile.

#include <foundation/core/Arithmetic.h>

static_assert(sizeof(::foundation::core::WideProduct<int>) == 8);

int main() { return 0; }
