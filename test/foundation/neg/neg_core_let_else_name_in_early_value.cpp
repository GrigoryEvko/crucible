// The name of CRUCIBLE_LET_ELSE is declared after its else arm, so the early
// value cannot read the value that the operand does not hold.

#include <foundation/core/Choice.h>

namespace {

[[nodiscard]] ::foundation::core::Option<int> positive(int value) noexcept {
    if (value <= 0) return ::foundation::core::none;
    return ::foundation::core::some(value);
}

[[nodiscard]] int kept_or_itself(int value) noexcept {
    CRUCIBLE_LET_ELSE(kept, positive(value), kept);
    return kept;
}

}  // namespace

int main() { return kept_or_itself(4) == 4 ? 0 : 1; }
