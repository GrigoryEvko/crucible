// An arm of the borrow match of an Option that is a named object of a class
// with a call operator, and that assigns the Option through a field of the
// object.  The assignment ends the payload that the reference of the arm
// names.  The rule borrow-match of the quarantine plugin reads the braced
// initializer of the object, and refuses the assignment.

#include <foundation/core/Choice.h>

struct Clearing {
    ::foundation::core::Option<int>& target;

    int operator()(int const& value) const {
        target = ::foundation::core::none;
        return value;
    }
};

int main() {
    ::foundation::core::Option<int> held = ::foundation::core::Option<int>::some(1);
    Clearing const arm{held};
    return held.match(arm, [] { return 0; });
}
