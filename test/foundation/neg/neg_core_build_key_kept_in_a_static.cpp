// The constructor of an arena object moves its build key into an object of static
// storage.  A build key has no move and no copy, so no code keeps it
// (ArenaBuildKey of foundation/core/Ref.h).

#include <foundation/ByteSeal.h>
#include <foundation/core/BumpArena.h>
#include <foundation/core/Ref.h>

#include <utility>

struct[[= ::foundation::brand::arena_family_brand{}]] Family final {};

namespace {
struct Node {
    using brand_type = Family;
    explicit Node(::foundation::core::ArenaBuildKey<Family, Node> key) noexcept {
        static ::foundation::core::ArenaBuildKey<Family, Node> kept{std::move(key)};
        static_cast<void>(kept);
    }
    Node(Node const&) = delete("a node stays in its arena");
    Node(Node&&) = delete("a node stays in its arena");
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};
};
}  // namespace

int main() { return sizeof(Node) == 0 ? 1 : 0; }
