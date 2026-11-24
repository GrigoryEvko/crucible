// Negative-compile test. This file must fail to compile.
//
// The body of an arm captures a linear end by move.  The match calls the
// body, and a call is no consume, so the end would end with the body.  The
// arm refuses a body that holds a linear value, and the deleted twin gives
// the reason.

#include <foundation/Linear.h>
#include <foundation/core/EnumMatch.h>

#include <cstdint>
#include <utility>

namespace {
struct[[= ::foundation::linear::linear{}]][[= ::foundation::lifetime::no_start_over_bytes{}]] End {
    int handle = 0;
    constexpr explicit End(int value) noexcept : handle{value} {}
    constexpr End(End&&) noexcept = default;
    constexpr End& operator=(End&& other) noexcept {
        handle = other.handle;
        return *this;
    }
    End(End const&) = delete("a linear end of the fixture does not copy");
    End& operator=(End const&) = delete("a linear end of the fixture does not copy");
};

enum class Strength : std::uint8_t {
    none
};
}  // namespace

int main() {
    End end{3};
    auto const arm = ::foundation::core::arm<Strength::none>([held = std::move(end)] noexcept { return held.handle; });
    static_cast<void>(arm);
    return 0;
}
