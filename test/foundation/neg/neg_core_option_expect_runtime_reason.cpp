// The reason of expect() is a constant array of static storage.  A
// pointer that the program calculates at run time can point at text that
// has no end or no longer exists, so it does not convert to the reason.

#include <foundation/core/Choice.h>

int main(int argument_count, char** arguments) {
    char const* reason = arguments[argument_count - 1];
    return ::foundation::core::Option<int>::some(1).expect(reason);
}
