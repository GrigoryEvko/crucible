// An arm of the match of a Link captures a linear end by move.  The match
// calls the arm, and a call is no consume, so the end would end with the arm.
// The match refuses an arm that holds a linear value.
#include <foundation/Linear.h>
#include <foundation/core/Link.h>
#include <foundation/core/Ref.h>

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

struct Chain final {};

struct Node {
    using brand_type = Chain;
    int value = 0;
};
}  // namespace

int main() {
    Node node{.value = 3};
    ::foundation::core::Link<Node, Chain> link = ::foundation::core::to(node);
    End end{7};
    return link.match(
        [held = std::move(end)](::foundation::core::Ref<Node, Chain> const& object) noexcept {
            return object->value + held.handle;
        },
        [] noexcept { return 0; });
}
