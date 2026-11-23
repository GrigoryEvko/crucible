// The function of the map_err of a Result captures a linear end by move.
// map_err calls the function, and a call is no consume, so the end would end
// with the function.  map_err refuses a function that holds a linear value.
#include <foundation/Linear.h>
#include <foundation/core/Choice.h>

#include <cstdint>
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

enum class Refusal : std::uint8_t {
    closed
};
}  // namespace

int main() {
    End end{1};
    auto mapped = ::foundation::core::Result<int, Refusal>{2}.map_err(
        [held = std::move(end)](Refusal refusal) noexcept { return held.handle == 0 ? refusal : Refusal::closed; });
    return static_cast<int>(mapped.is_ok());
}
