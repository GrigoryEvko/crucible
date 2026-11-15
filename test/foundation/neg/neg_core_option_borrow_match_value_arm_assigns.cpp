// An arm of the borrow match of an Option that takes its payload by value and
// captures the Option by reference.  A parameter by value can hold a borrow
// of the payload, such as a View that a conversion makes, so the rule
// borrow-match of the quarantine plugin refuses the assignment of the Option
// in such an arm too.

#include <foundation/core/Choice.h>

int main() {
    ::foundation::core::Option<int> held = ::foundation::core::Option<int>::some(1);
    return held.match(
        [&held](int value) {
            held = ::foundation::core::none;
            return value;
        },
        [] { return 0; });
}
