// CRUCIBLE_TRY consumes its operand.  A local without a cast to an rvalue
// would stay full after the macro took its value, so the macro refuses an
// lvalue.

#include <foundation/core/Choice.h>

#include <cstdint>

enum class Code : std::uint8_t {
    full
};

namespace {

[[nodiscard]] ::foundation::core::Result<int, Code> half_of(int value) noexcept {
    ::foundation::core::Result<int, Code> held = value / 2;
    CRUCIBLE_TRY(half, held);
    return half;
}

}  // namespace

int main() { return half_of(4).is_ok() ? 0 : 1; }
