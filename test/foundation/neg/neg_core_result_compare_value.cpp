// A Result compares only with a Result of its own value and error types.
// The implicit constructor from a value makes no compare: the state has its
// own test, is_ok().

#include <foundation/core/Choice.h>

#include <cstdint>

enum class Code : std::uint8_t {
    full,
    closed
};

int main() {
    ::foundation::core::Result<int, Code> const outcome{5};
    return outcome == 5 ? 0 : 1;
}
