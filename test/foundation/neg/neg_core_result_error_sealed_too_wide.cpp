// A sealed error of more than 16 bytes is refused, as a plain error of more
// than 16 bytes is.  An error stays small, so a Result of a small value goes
// back in registers.

#include <foundation/ByteSeal.h>
#include <foundation/core/Choice.h>

#include <cstdint>

struct WideSealedError {
    std::uint64_t first = 0;
    std::uint64_t second = 0;
    std::uint64_t third = 0;
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal{};
};

int main() { return sizeof(::foundation::core::Result<int, WideSealedError>) > 0 ? 0 : 1; }
