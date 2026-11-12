// Each operation of a Result is noexcept, so a Result takes an Err of a
// different error only through a conversion that does not throw.

#include <foundation/core/Choice.h>

#include <cstdint>

enum class Code : std::uint8_t {
    full
};

struct NetError {
    Code code = Code::full;
    constexpr NetError(Code from) : code{from} {}
};

namespace {

[[nodiscard]] ::foundation::core::Result<int, NetError> refuse() noexcept {
    return ::foundation::core::err(Code::full);
}

}  // namespace

int main() { return refuse().is_ok() ? 0 : 1; }
