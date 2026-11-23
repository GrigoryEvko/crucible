// The init of mint_heap_array_with captures a linear end by move.  The mint
// calls init for each position and ends it, and a call is no consume, so the
// end would end with init.  The mint refuses an init that holds a linear
// value, and the deleted twin gives the reason.
#include <foundation/Linear.h>
#include <foundation/core/Array.h>
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
    auto const built = ::foundation::core::mint_heap_array_with<int>(
        ::foundation::effects::testing::test().alloc, 4,
        [held = std::move(end)](std::size_t) noexcept { return held.handle; });
    static_cast<void>(built);
    return 0;
}
