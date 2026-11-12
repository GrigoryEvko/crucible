// A FixedArray has no operator[]: an index from data goes through
// Index<N>::check, so no read takes a number that no check proved.

#include <foundation/core/Array.h>

#include <cstddef>

inline int unchecked_read(::foundation::core::FixedArray<int, 4> const& numbers, std::size_t position) {
    return numbers[position];
}

int main() {}
