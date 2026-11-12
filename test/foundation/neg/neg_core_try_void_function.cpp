// CRUCIBLE_TRY returns the failure to the caller.  A function that returns
// void cannot give the failure back, so the return is refused.

#include <foundation/core/Choice.h>

#include <cstdint>

enum class Code : std::uint8_t {
    full
};

namespace {

int total = 0;

[[nodiscard]] ::foundation::core::Result<int, Code> half_of(int value) noexcept { return value / 2; }

void add_half(int value) noexcept {
    CRUCIBLE_TRY(half, half_of(value));
    total += half;
}

}  // namespace

int main() {
    add_half(4);
    return total == 2 ? 0 : 1;
}
