// parse reads only an integer: parse of a text to a double does not compile.

#include <foundation/core/Parse.h>

int main() { return ::foundation::core::parse<double>("7").is_ok() ? 0 : 1; }
