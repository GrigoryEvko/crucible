// The predicate of find_if captures a linear end by move.  The door calls the
// predicate for each element and ends it, and a call is no consume, so the end
// would end with the predicate.  The door refuses a predicate that holds a
// linear value.
#include <foundation/Linear.h>
#include <foundation/core/Algorithms.h>
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
    End end{3};
    auto const found = ::foundation::core::find_if(
        values.view(), [held = std::move(end)](int const& value) noexcept { return value == held.handle; });
    return found.is_some() ? 0 : 1;
}
