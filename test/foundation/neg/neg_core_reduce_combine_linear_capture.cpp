// The combine of a tree reduction captures a linear end by move.  The door
// calls the combine for each element and ends it, and a call is no consume, so
// the end would end with the combine.  The door refuses a combine that holds
// a linear value.
#include <foundation/Linear.h>
#include <foundation/core/Array.h>
#include <foundation/core/Reduce.h>

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
    auto const reduced = ::foundation::core::reduce_by_tree(
        values.view(),
        [held = std::move(end)](int const& left, int const& right) noexcept { return left + right + held.handle; });
    return reduced.is_some() ? 0 : 1;
}
