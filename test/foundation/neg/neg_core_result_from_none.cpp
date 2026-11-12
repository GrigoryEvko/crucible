// A Result has no empty state, and none carries no error.  The deleted
// constructor tells the author to give the error with ok_or().

#include <foundation/core/Choice.h>

#include <cstdint>

enum class Code : std::uint8_t {
    full
};

namespace {

[[nodiscard]] ::foundation::core::Result<int, Code> refuse() noexcept { return ::foundation::core::none; }

}  // namespace

int main() { return refuse().is_ok() ? 0 : 1; }
