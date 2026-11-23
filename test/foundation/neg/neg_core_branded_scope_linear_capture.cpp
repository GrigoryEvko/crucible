// The body of a branded scope captures a linear end by move.  The scope mint
// calls the body, and a call is no consume, so the end would end with the
// body.  The mint refuses a body that holds a linear value.
#include <foundation/Linear.h>
#include <foundation/core/Array.h>
#include <foundation/core/Branded.h>

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
    ::foundation::core::FixedArray<int, 4> const values{1, 2, 3, 4};
    End end{1};
    return ::foundation::core::mint_branded_scope(values.view(), [held = std::move(end)](auto const& view) noexcept {
        return static_cast<int>(view.size()) + held.handle;
    });
}
