// A NaN is not <= each value, so a range with a NaN bound does not compile.

#include <foundation/core/Compare.h>

int main() { return ::foundation::core::bounds<::foundation::core::quiet_nan_of<double>, 1.0>().hi() > 0.0 ? 0 : 1; }
