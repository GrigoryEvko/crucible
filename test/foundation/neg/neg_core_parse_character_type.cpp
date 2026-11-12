// A character is not a number: parse of a text to a char does not compile.

#include <foundation/core/Parse.h>

int main() { return ::foundation::core::parse<char>("7").is_ok() ? 0 : 1; }
