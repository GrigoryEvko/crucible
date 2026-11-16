// A Box is linear when its payload is linear, and the Box reads its payload
// where a unit first names the Box.  A node that holds a Box of its own type
// names the Box before the node is complete, so that Box is not linear.  The
// end of such a Box stops the build, so the drop of a linear node cannot
// pass with no consume.

#include <foundation/Linear.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Ref.h>
#include <foundation/effects/Effect.h>

#include <utility>

struct[[= ::foundation::linear::linear{}]] Node {
    ::foundation::core::Option<::foundation::core::Box<Node>> next{};

    Node() noexcept = default;
    Node(Node&&) noexcept = default;
    Node& operator=(Node&& /*other*/) noexcept { return *this; }
    Node(Node const&) = delete("a node of the fixture does not copy");
    Node& operator=(Node const&) = delete("a node of the fixture does not copy");
};

int main() {
    ::foundation::core::Box<Node> head =
        ::foundation::core::mint_box<Node>(::foundation::effects::testing::test().alloc);
    Node const node = std::move(head).consume();
    return node.next.is_some() ? 1 : 0;
}
