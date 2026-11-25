// The constructor of an arena object gives a Ref of its build key to its caller, and
// the caller could ask the key for the key of a base after the constructor ends.  A
// Ref refuses a build key as its target (ArenaBuildKey of foundation/core/Ref.h).

#include <foundation/ByteSeal.h>
#include <foundation/core/BumpArena.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Ref.h>

struct[[= ::foundation::brand::arena_family_brand{}]] Family final {};

namespace {
struct Node {
    using brand_type = Family;
    explicit Node(::foundation::core::ArenaBuildKey<Family, Node> /*key*/) noexcept {}
    Node(Node const&) = delete("a node stays in its arena");
    Node(Node&&) = delete("a node stays in its arena");
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};
};
struct KeptBrand final {};
using KeptKey = ::foundation::core::Ref<::foundation::core::ArenaBuildKey<Family, Node>, KeptBrand>;
}  // namespace

int main() { return sizeof(KeptKey) == 0 ? 1 : 0; }
