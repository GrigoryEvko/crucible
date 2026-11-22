// The body of the element door of a FixedArray captures a linear end by
// move.  The door calls the body, and a call is no consume, so the end would
// end with the body.  The door refuses a body that holds a linear value.
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
    ::foundation::core::FixedArray<int, 2> counts{};
    End end{7};
    counts.with_element(::foundation::core::index<0>(),
                        [held = std::move(end)](int& count) noexcept { count = held.handle; });
    return counts.at(::foundation::core::index<0>());
}
