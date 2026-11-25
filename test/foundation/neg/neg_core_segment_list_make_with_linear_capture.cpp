// The make of append_with captures a linear end by move.  The list calls make
// and does not consume it, so the end would end with make.  append_with refuses
// a make that holds a linear value, and the deleted twin gives the reason.

#include <foundation/Linear.h>
#include <foundation/core/SegmentList.h>
#include <foundation/effects/Effect.h>

#include <cstddef>
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
    End end{9};
    ::foundation::core::SegmentList<int> numbers;
    numbers.append_with(::foundation::effects::testing::test().alloc,
                        [held = std::move(end)](std::size_t) noexcept { return held.handle; });
    return static_cast<int>(numbers.size());
}
