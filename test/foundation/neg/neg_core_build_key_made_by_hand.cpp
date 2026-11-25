// Code outside an arena builds the build key of an arena object and then the object
// on the stack.  The constructor of a key is private, and only the door of the arena
// calls it (ArenaBuildKey of foundation/core/Ref.h).

#include <foundation/ByteSeal.h>
#include <foundation/core/BumpArena.h>
#include <foundation/core/Ref.h>

struct[[= ::foundation::brand::arena_family_brand{}]] Family final {};

namespace {
struct Node {
    using brand_type = Family;
    explicit Node(::foundation::core::ArenaBuildKey<Family, Node> /*key*/) noexcept {}
    Node(Node const&) = delete("a node stays in its arena");
    Node(Node&&) = delete("a node stays in its arena");
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};
    int value = 0;
};
}  // namespace

int main() {
    Node const node{::foundation::core::ArenaBuildKey<Family, Node>{}};
    return node.value;
}
