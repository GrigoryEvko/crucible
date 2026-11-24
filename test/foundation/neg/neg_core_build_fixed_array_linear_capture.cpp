// The body of build_fixed_array captures a linear end by move.  The door calls
// the body, and a call is no consume, so the end would end with the body.
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
    End end{7};
    auto const built = ::foundation::core::build_fixed_array<int, 2>([held = std::move(end)](auto run) noexcept {
        for (int& element : run)
            element = held.handle;
    });
    return built.at(::foundation::core::index<0>());
}
