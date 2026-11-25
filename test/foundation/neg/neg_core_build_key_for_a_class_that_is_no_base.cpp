// The constructor of an arena object asks its build key for the key of a class that
// is no base of it, and it builds an object of that class on the stack.  A key gives
// the key of a base alone (ArenaBuildKey of foundation/core/Ref.h).

#include <foundation/ByteSeal.h>
#include <foundation/core/BumpArena.h>
#include <foundation/core/Ref.h>

#include <utility>

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

struct Maker {
    explicit Maker(::foundation::core::ArenaBuildKey<Family, Maker> key) noexcept {
        static_cast<void>(std::move(key).for_base<Node>());
    }
};
}  // namespace

int main() { return sizeof(Maker) == 0 ? 1 : 0; }
