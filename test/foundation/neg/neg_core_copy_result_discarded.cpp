// A copy between two Views of a dynamic extent can find two different
// counts.  The Result is [[nodiscard]], so a caller cannot drop the error
// and read a run that the copy did not write.

#include <foundation/core/Region.h>

#include <cstdint>

inline void copy_and_drop(::foundation::core::View<std::uint64_t> target,
                          ::foundation::core::View<std::uint64_t const> source) {
    ::foundation::core::copy(target, source);
}

int main() { return 0; }
