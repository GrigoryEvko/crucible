// CRUCIBLE_TRY of a Result returns the Err of its error.  A function that
// returns an Option has no place for the error, so the return is refused.

#include <foundation/core/Choice.h>

#include <cstdint>

enum class Code : std::uint8_t {
    full
};

namespace {

[[nodiscard]] ::foundation::core::Result<int, Code> half_of(int value) noexcept { return value / 2; }

[[nodiscard]] ::foundation::core::Option<int> quarter_of(int value) noexcept {
    CRUCIBLE_TRY(half, half_of(value));
    return ::foundation::core::some(half / 2);
}

}  // namespace

int main() { return quarter_of(8).is_some() ? 0 : 1; }
