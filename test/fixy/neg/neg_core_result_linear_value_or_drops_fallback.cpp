// value_or of a Result that holds a value gives the value and drops the
// fallback.  A dropped linear value ends with no door, so a Result of a
// linear value has no value_or.

#include <fixy/Core.h>
#include <fixy/Qtt.h>

#include <cstdint>
#include <utility>

namespace {
enum class Refusal : std::uint8_t {
    full,
};
}  // namespace

int main() {
    fixy::Result<fixy::Linear<int>, Refusal> result = fixy::mint_linear<int>(7);
    static_cast<void>(std::move(result).value_or(fixy::mint_linear<int>(0)));
    return 0;
}
