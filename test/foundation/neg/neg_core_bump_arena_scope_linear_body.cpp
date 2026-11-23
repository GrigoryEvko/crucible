// The body of with_bump_arena captures a linear end by move.  The door calls
// the body, and a call is no consume, so the end would end with the body.
// The door refuses a body that holds a linear value.
#include <foundation/Linear.h>
#include <foundation/core/BumpArena.h>
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
    End end{7};
    return ::foundation::core::with_bump_arena(
        ::foundation::effects::testing::test().alloc,
        ::foundation::core::Quantity<::foundation::core::unit::Byte, std::size_t>{4096},
        [held = std::move(end)](auto& arena) noexcept {
            return held.handle + static_cast<int>(arena.retained_bytes());
        });
}
