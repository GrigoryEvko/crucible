// An arm of the rvalue match of an Option captures a linear end by move.  The
// match calls the arm, and a call is no consume, so the end would end with the
// arm.  The match refuses an arm that holds a linear value.
#include <foundation/Linear.h>
#include <foundation/core/Choice.h>

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
}  // namespace

int main() {
    End end{7};
    return ::foundation::core::Option<int>::some(3).match(
        [held = std::move(end)](int value) noexcept { return value + held.handle; }, [] noexcept { return 0; });
}
