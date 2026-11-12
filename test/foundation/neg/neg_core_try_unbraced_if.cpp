// CRUCIBLE_TRY is a sequence of statements.  The body of an if with no
// braces holds only the first statement, so the next statements do not
// find the hidden local, and the compile stops.

#include <foundation/core/Choice.h>

#include <cstdint>

enum class Code : std::uint8_t {
    full
};

namespace {

[[nodiscard]] ::foundation::core::Result<int, Code> half_of(int value) noexcept { return value / 2; }

[[nodiscard]] ::foundation::core::Result<int, Code> half_when(bool is_wanted, int value) noexcept {
    if (is_wanted) CRUCIBLE_TRY(half, half_of(value));
    return value;
}

}  // namespace

int main() { return half_when(true, 8).is_ok() ? 0 : 1; }
