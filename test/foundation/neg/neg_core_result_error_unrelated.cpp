// A Result takes an Err of a different error only when that error converts
// to its own error.  An unrelated error has no conversion, so the deleted
// constructor tells the author to map the error.

#include <foundation/core/Choice.h>

#include <cstdint>

enum class Code : std::uint8_t {
    full
};

enum class Other : std::uint8_t {
    late
};

namespace {

[[nodiscard]] ::foundation::core::Result<int, Code> refuse() noexcept { return ::foundation::core::err(Other::late); }

}  // namespace

int main() { return refuse().is_ok() ? 0 : 1; }
