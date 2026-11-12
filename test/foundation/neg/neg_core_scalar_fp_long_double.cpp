// fp::is_nan takes a Float: long double has 80 value bits on x86_64 and 128
// on aarch64, so it does not compile.

#include <foundation/core/Float.h>

int main() { return ::foundation::core::fp::is_nan(1.0L) ? 1 : 0; }
