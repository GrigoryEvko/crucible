// The generator of a vec captures a linear end by move.  The constructor
// calls the generator for each lane and ends it, and a call is no consume, so
// the end would end with the generator.  The constructor refuses a generator
// that holds a linear value, and the deleted twin gives the reason.
#include <foundation/Linear.h>
#include <foundation/Simd.h>

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
    End end{8};
    ::foundation::simd::vec<int, 4> const lanes{
        [held = std::move(end)](auto lane) noexcept { return held.handle + decltype(lane)::value; }};
    static_cast<void>(lanes);
    return 0;
}
