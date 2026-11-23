// The function of the map of a walk captures a linear end by move.  The walk
// calls the function for each element and ends it with the walk, and a call
// is no consume, so the end would end with the walk.  The map refuses a
// function that holds a linear value.
#include <foundation/Linear.h>
#include <foundation/core/Array.h>

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
    ::foundation::core::FixedArray<int, 4> const values{1, 2, 3, 4};
    End end{7};
    return values.view()
        .walk()
        .map([held = std::move(end)](int const& value) noexcept { return value + held.handle; })
        .fold(0, [](int sum, int value) noexcept { return sum + value; });
}
