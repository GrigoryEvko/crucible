// CRUCIBLE_TRY returns the failure to the caller.  A constructor gives no
// value back, so the return is refused.  A mint that returns a Result is
// the place for the macro.

#include <foundation/core/Choice.h>

#include <cstdint>

enum class Code : std::uint8_t {
    full
};

namespace {

[[nodiscard]] ::foundation::core::Result<int, Code> half_of(int value) noexcept { return value / 2; }

struct Halved {
    int half = 0;

    explicit Halved(int value) noexcept {
        CRUCIBLE_TRY(kept, half_of(value));
        half = kept;
    }
};

}  // namespace

int main() { return Halved{4}.half == 2 ? 0 : 1; }
