// The hidden local of CRUCIBLE_TRY takes its name from the line, so one
// line holds one CRUCIBLE_TRY.  A second one on the same line declares the
// same local again, and the compile stops.

#include <foundation/core/Choice.h>

#include <cstdint>

enum class Code : std::uint8_t {
    full
};

namespace {

[[nodiscard]] ::foundation::core::Result<int, Code> half_of(int value) noexcept { return value / 2; }

// clang-format off
[[nodiscard]] ::foundation::core::Result<int, Code> quarter_of(int value) noexcept {
    CRUCIBLE_TRY(half, half_of(value)); CRUCIBLE_TRY(quarter, half_of(half));
    return quarter;
}
// clang-format on

}  // namespace

int main() { return quarter_of(8).is_ok() ? 0 : 1; }
