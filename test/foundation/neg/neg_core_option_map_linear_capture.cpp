// The function of the map of an Option captures a linear end by move.  map
// calls the function, and a call is no consume, so the end would end with the
// function.  map refuses a function that holds a linear value.
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
    End end{1};
    auto mapped = ::foundation::core::Option<int>::some(2).map(
        [held = std::move(end)](int value) noexcept { return value + held.handle; });
    return static_cast<int>(mapped.is_some());
}
