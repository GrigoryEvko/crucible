// The bytes of a temporary object would dangle at the end of the full
// expression.

#include <foundation/core/Algorithms.h>

#include <cstdint>

inline auto image_of_temporary() { return ::foundation::core::bytes(std::uint64_t{7}); }

int main() {}
