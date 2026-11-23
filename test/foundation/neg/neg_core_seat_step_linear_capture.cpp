// The function of a step of a Seat captures a linear end by move.  The step
// calls the function and ends it, and a call is no consume, so the end would
// end with the function.  The step refuses a function that holds a linear
// value, and the deleted twin gives the reason.
#include <foundation/Linear.h>
#include <foundation/core/Seat.h>

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
    ::foundation::core::Seat<int> seat = ::foundation::core::mint_seat(1);
    End end{2};
    seat.step([held = std::move(end)](int&& value) noexcept { return value + held.handle; });
    return 0;
}
