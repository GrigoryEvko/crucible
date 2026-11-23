// The call of result_of_restarted captures a linear end by move.  The door
// calls it until the kernel gives no eintr and ends it, and a call is no
// consume, so the end would end with the call.  The door refuses a call that
// holds a linear value, and the deleted twin gives the reason.
#include <foundation/Linear.h>
#include <foundation/os/Errno.h>

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
    auto const result =
        ::foundation::os::result_of_restarted([held = std::move(end)]() noexcept { return long{held.handle}; });
    static_cast<void>(result);
    return 0;
}
