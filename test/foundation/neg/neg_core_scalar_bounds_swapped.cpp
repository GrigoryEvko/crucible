// bounds<Lo, Hi>() proves Lo <= Hi at compile time: a range with its bounds
// in the wrong order does not compile.

#include <foundation/core/Compare.h>

int main() { return ::foundation::core::bounds<10, 0>().lo(); }
