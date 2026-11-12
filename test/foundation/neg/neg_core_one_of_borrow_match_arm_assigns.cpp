// An arm of the borrow match of a OneOf that assigns the OneOf ends the
// alternative that the reference of the arm names.  The rule borrow-match of
// the quarantine plugin refuses the assignment.

#include <foundation/core/OneOf.h>

using Number = ::foundation::core::OneOf<int, long>;

int main() {
    Number held{3};
    return held.match(
        [&](int const& value) {
            held = Number{4L};
            return value;
        },
        [](long const& value) { return static_cast<int>(value); });
}
