// An Option has no place for an error, so an Err does not convert to an
// Option.  The deleted constructor tells the author to take the value of a
// Result with ok().

#include <foundation/core/Choice.h>

#include <cstdint>

enum class Code : std::uint8_t {
    full
};

namespace {

[[nodiscard]] ::foundation::core::Option<int> refuse() noexcept { return ::foundation::core::err(Code::full); }

}  // namespace

int main() { return refuse().is_some() ? 0 : 1; }
