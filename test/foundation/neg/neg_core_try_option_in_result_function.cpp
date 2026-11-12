// CRUCIBLE_TRY of an Option returns none.  none carries no error, so a
// function that returns a Result refuses the return.

#include <foundation/core/Choice.h>

#include <cstdint>

enum class Code : std::uint8_t {
    full
};

namespace {

[[nodiscard]] ::foundation::core::Option<int> positive(int value) noexcept {
    if (value <= 0) return ::foundation::core::none;
    return ::foundation::core::some(value);
}

[[nodiscard]] ::foundation::core::Result<int, Code> doubled(int value) noexcept {
    CRUCIBLE_TRY(kept, positive(value));
    return kept * 2;
}

}  // namespace

int main() { return doubled(4).is_ok() ? 0 : 1; }
