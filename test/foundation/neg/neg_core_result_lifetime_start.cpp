// Zero bytes are a Result that no constructor made: a value, or the error
// of the first enumerator.  The checked lifetime start refuses each Result.

#include <foundation/Lifetime.h>
#include <foundation/core/Choice.h>

#include <cstdint>

enum class Code : std::uint8_t {
    full
};

int main() {
    std::uint64_t word = 0;
    static_cast<void>(
        ::foundation::lifetime::start_as_array<::foundation::core::Result<std::uint32_t, Code>>(&word, 1));
    return 0;
}
