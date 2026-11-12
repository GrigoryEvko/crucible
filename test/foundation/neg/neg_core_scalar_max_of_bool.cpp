// bool is not a number: a limit of bool does not compile, where
// std::numeric_limits<bool>::max() gives true with no message.

#include <foundation/core/Number.h>

int main() { return ::foundation::core::max_of<bool> ? 0 : 1; }
