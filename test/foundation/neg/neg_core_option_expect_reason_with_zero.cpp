// The report writes the reason of expect() up to its first zero byte.  A
// zero byte inside the reason would cut the text, so it stops the build at
// the call.

#include <foundation/core/Choice.h>

int main() { return ::foundation::core::Option<int>::some(1).expect("the option\0 holds a value"); }
