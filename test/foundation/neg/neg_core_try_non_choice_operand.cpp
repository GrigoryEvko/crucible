// CRUCIBLE_TRY takes the value of an Option or a Result.  A plain value has
// no failure to return, so the macro refuses it.

#include <foundation/core/Choice.h>

#include <cstdint>

enum class Code : std::uint8_t {
    full
};

namespace {

[[nodiscard]] ::foundation::core::Result<int, Code> twice(int value) noexcept {
    CRUCIBLE_TRY(kept, value * 2);
    return kept;
}

}  // namespace

int main() { return twice(4).is_ok() ? 0 : 1; }
