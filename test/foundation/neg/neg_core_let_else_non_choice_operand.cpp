// CRUCIBLE_LET_ELSE takes the value of an Option or a Result.  A plain value
// has no failure, so the macro refuses it.

#include <foundation/core/Choice.h>

namespace {

[[nodiscard]] int twice(int value) noexcept {
    CRUCIBLE_LET_ELSE(kept, value * 2, 0);
    return kept;
}

}  // namespace

int main() { return twice(4) == 8 ? 0 : 1; }
