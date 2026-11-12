// A FixedArray takes exactly N arguments, so a list of fewer arguments
// leaves no element with a value that no code chose.

#include <foundation/core/Array.h>

inline int first_of_partial() {
    ::foundation::core::FixedArray<int, 3> numbers{1, 2};
    return numbers.at(::foundation::core::index<0>());
}

int main() {}
