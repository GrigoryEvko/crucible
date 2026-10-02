// An error carries its meaning in its type: a scoped enum or a class.  A
// plain integer could be a count, a code or an errno, so the error gate
// refuses it.

#include <foundation/core/Choice.h>

int main() { return sizeof(::foundation::core::Result<int, int>) > 0 ? 0 : 1; }
