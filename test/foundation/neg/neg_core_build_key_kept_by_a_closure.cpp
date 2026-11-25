// The constructor of an arena object keeps a closure that captures its build key by
// reference, and the closure could ask the key for the key of a base after the
// constructor ends.  The rule view-escape refuses a borrow of a parameter in the
// object (ArenaBuildKey of foundation/core/Ref.h).

#include <foundation/ByteSeal.h>
#include <foundation/core/BumpArena.h>
#include <foundation/core/Ref.h>

#include <utility>

struct[[= ::foundation::brand::arena_family_brand{}]] Family final {};

namespace {
struct Node;

template <class Parameter>
auto keep_by_reference(Parameter& parameter) noexcept {
    return [&parameter]() noexcept { static_cast<void>(parameter); };
}
using Closure = decltype(keep_by_reference(std::declval<::foundation::core::ArenaBuildKey<Family, Node>&>()));

struct Node {
    explicit Node(::foundation::core::ArenaBuildKey<Family, Node> key) noexcept : kept{keep_by_reference(key)} {}
    Node(Node const&) = delete("a node stays in its arena");
    Node(Node&&) = delete("a node stays in its arena");
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};
    Closure kept;
};
}  // namespace

int main() { return sizeof(Node) == 0 ? 1 : 0; }
