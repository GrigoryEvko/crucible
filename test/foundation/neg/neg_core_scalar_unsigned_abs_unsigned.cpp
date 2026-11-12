// unsigned_abs takes a signed value.  The magnitude of an unsigned value is
// the value itself, so a call with an unsigned value does not compile.

#include <foundation/core/Compare.h>

int main() { return ::foundation::core::unsigned_abs(3u) == 3u ? 0 : 1; }
