// Tier 1, the function shape.  A function type is not an object type: it
// has no storage and cannot be held by value.  It is also the shape most
// likely to be written by accident, because the Type axis names "the
// function or callable a binding grades" and a reader may reach for the
// function type itself.
//
// The payload to name instead is a pointer to the function, or the
// callable's own class type, both of which are object types a binding
// can hold.

#include <fixy/Fn.h>

int main() {
    [[maybe_unused]] ::fixy::fn<int()> refused{};
    return 0;
}
