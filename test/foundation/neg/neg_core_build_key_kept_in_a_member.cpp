// The constructor of an arena object keeps its build key in a member, and it could
// build a second object with the key later.  A build key has no move and no copy, so
// no code keeps it (ArenaBuildKey of foundation/core/Ref.h).

#include <foundation/ByteSeal.h>
#include <foundation/core/BumpArena.h>
#include <foundation/core/Ref.h>

#include <utility>

struct[[= ::foundation::brand::arena_family_brand{}]] Family final {};

namespace {
struct Node {
    using brand_type = Family;
    explicit Node(::foundation::core::ArenaBuildKey<Family, Node> key) noexcept : kept{std::move(key)} {}
    Node(Node const&) = delete("a node stays in its arena");
    Node(Node&&) = delete("a node stays in its arena");
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};
    ::foundation::core::ArenaBuildKey<Family, Node> kept;
};
}  // namespace

int main() { return sizeof(Node) == 0 ? 1 : 0; }
