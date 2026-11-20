// A Box reads its payload where a unit first names the Box, and its two
// assignments read that one fact.  A node that holds a Box of its own type
// names the Box before the node is complete, so that Box is not linear, and
// its assignment is no deleted function.  The assignment ends the node that
// the target holds, and that end stops the build, so an assignment cannot
// drop a linear node either.

#include <foundation/Linear.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Ref.h>
#include <foundation/effects/Effect.h>

struct[[= ::foundation::linear::linear{}]] Node {
    ::foundation::core::Option<::foundation::core::Box<Node>> next{};

    Node() noexcept = default;
    Node(Node&&) noexcept = default;
    Node& operator=(Node&& /*other*/) noexcept { return *this; }
    Node(Node const&) = delete("a node of the fixture does not copy");
    Node& operator=(Node const&) = delete("a node of the fixture does not copy");
};

int main() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    ::foundation::core::Box<Node> target = ::foundation::core::mint_box<Node>(alloc);
    target = ::foundation::core::mint_box<Node>(alloc);
    return 0;
}
