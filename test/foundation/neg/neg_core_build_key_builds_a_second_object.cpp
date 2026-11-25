// The constructor of an arena object builds a second object of its class on the
// stack with its build key.  The constructor takes the key by value, and the key
// has no move, so the second object gets no key (ArenaBuildKey of
// foundation/core/Ref.h).

#include <foundation/ByteSeal.h>
#include <foundation/core/BumpArena.h>
#include <foundation/core/Ref.h>

#include <utility>

struct[[= ::foundation::brand::arena_family_brand{}]] Family final {};

namespace {
struct Node {
    using brand_type = Family;
    explicit Node(::foundation::core::ArenaBuildKey<Family, Node> key) noexcept {
        if (value == 0) {
            Node second{std::move(key)};
            value = second.value + 1;
        }
    }
    Node(Node const&) = delete("a node stays in its arena");
    Node(Node&&) = delete("a node stays in its arena");
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};
    int value = 0;
};
}  // namespace

int main() { return sizeof(Node) == 0 ? 1 : 0; }
