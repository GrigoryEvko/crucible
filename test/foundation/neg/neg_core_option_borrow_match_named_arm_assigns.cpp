// An arm of the borrow match of an Option that the call gets by name, as a
// local closure, and that assigns the Option.  The assignment ends the payload
// that the reference of the arm names.  The rule borrow-match of the
// quarantine plugin reads the initializer of the local, and refuses the
// assignment.

#include <foundation/core/Choice.h>

int main() {
    ::foundation::core::Option<int> held = ::foundation::core::Option<int>::some(1);
    auto const arm = [&held](int const& value) {
        held = ::foundation::core::none;
        return value;
    };
    return held.match(arm, [] { return 0; });
}
