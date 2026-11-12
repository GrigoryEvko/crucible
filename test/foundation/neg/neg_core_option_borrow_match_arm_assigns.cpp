// An arm of the borrow match of an Option that assigns the Option ends the
// payload that the reference of the arm names.  The rule borrow-match of the
// quarantine plugin refuses the assignment.

#include <foundation/core/Choice.h>

int main() {
    ::foundation::core::Option<int> held = ::foundation::core::Option<int>::some(1);
    return held.match(
        [&](int const& value) {
            held = ::foundation::core::none;
            return value;
        },
        [] { return 0; });
}
