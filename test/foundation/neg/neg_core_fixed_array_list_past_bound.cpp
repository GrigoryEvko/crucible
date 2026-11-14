// A FixedArray of more than max_listed_array_count elements has no list
// constructor, so a list of 257 values does not compile.  The bound keeps
// the declaration of the constructor small for a large array.

#include <foundation/core/Array.h>

#include <cstddef>
#include <utility>

template <std::size_t... Positions>
inline int first_of_long_list(std::index_sequence<Positions...>) {
    ::foundation::core::FixedArray<int, sizeof...(Positions)> numbers{static_cast<int>(Positions)...};
    return numbers.at(::foundation::core::index<0>());
}

int main() { return first_of_long_list(std::make_index_sequence<257>{}); }
