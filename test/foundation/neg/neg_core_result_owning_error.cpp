// An error is a value that a copy of its bytes moves.  A class whose copy
// runs code could own memory or count its copies, so the error gate
// refuses it.

#include <foundation/core/Choice.h>

struct CountedError {
    int count = 0;
    CountedError() = default;
    CountedError(CountedError const& other) noexcept : count{other.count + 1} {}
};

int main() { return sizeof(::foundation::core::Result<int, CountedError>) > 0 ? 0 : 1; }
