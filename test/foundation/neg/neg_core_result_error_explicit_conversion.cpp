// The return of an Err shows no conversion, so a Result takes an Err of a
// different error only through an implicit conversion.  An explicit
// constructor of the error is refused.

#include <foundation/core/Choice.h>

#include <cstdint>

enum class Code : std::uint8_t {
    full
};

struct NetError {
    Code code = Code::full;
    explicit constexpr NetError(Code from) noexcept : code{from} {}
};

namespace {

[[nodiscard]] ::foundation::core::Result<int, NetError> refuse() noexcept {
    return ::foundation::core::err(Code::full);
}

}  // namespace

int main() { return refuse().is_ok() ? 0 : 1; }
