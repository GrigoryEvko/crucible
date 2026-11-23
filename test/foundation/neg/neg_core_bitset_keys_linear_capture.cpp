// The body of a walk of the keys of a BitSet captures a linear end by move.
// The door calls the body and ends it, and a call is no consume, so the end
// would end with the body.  The door refuses a body that holds a linear
// value, and the deleted twin gives the reason.
#include <foundation/Linear.h>
#include <foundation/core/BitSet.h>

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
    ::foundation::core::BitSet<64> const set{};
    End end{4};
    int const seen = set.with_keys([held = std::move(end)](auto const& keys) noexcept {
        static_cast<void>(keys);
        return held.handle;
    });
    return seen == 4 ? 0 : 1;
}
