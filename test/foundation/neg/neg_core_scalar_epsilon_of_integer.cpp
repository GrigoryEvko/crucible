// The constants of a Float do not exist for an integer, where
// std::numeric_limits<int>::epsilon() gives 0 with no message.

#include <foundation/core/Number.h>

int main() { return ::foundation::core::epsilon_of<int>; }
