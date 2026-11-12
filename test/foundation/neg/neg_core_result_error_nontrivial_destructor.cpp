// An error whose destructor does something is not a value of its bytes.
// The end of a Result could then run code that the caller does not see, so
// the error gate refuses it.

#include <foundation/core/Choice.h>

#include <cstdint>

struct LoggedError {
    std::uint32_t code = 0;
    ~LoggedError() {}
};

int main() { return sizeof(::foundation::core::Result<int, LoggedError>) > 0 ? 0 : 1; }
