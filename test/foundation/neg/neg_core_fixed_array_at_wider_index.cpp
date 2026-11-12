// An Index<8> proves a number below 8, so it is no proof for an array of 4
// elements.

#include <foundation/core/Array.h>

inline int read_past(::foundation::core::FixedArray<int, 4> const& numbers, ::foundation::core::Index<8> position) {
    return numbers.at(position);
}

int main() {}
