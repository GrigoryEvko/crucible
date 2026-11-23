// The function of map of a Computation captures a linear end by move.  The
// door calls the function and ends it, and a call is no consume, so the end
// would end with the function.  map refuses a function that holds a linear
// value, and the deleted twin gives the reason.
#include <foundation/Linear.h>
#include <foundation/effects/Computation.h>

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

namespace fe = ::foundation::effects;

int main() {
    fe::Computation<fe::Row<>, int> computation = fe::Computation<fe::Row<>, int>::mint_computation(1);
    End end{6};
    auto mapped =
        std::move(computation).map([held = std::move(end)](int value) noexcept { return value + held.handle; });
    static_cast<void>(mapped);
    return 0;
}
