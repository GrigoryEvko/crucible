// An arm of the match of a Link keeps the Ref that the Link lends in an Option
// that it captures by reference, as an arm of the borrow match of an Option
// does.  The store check refuses the arm (LentArmStoresNoBorrow of
// foundation/core/Choice.h), and the store twin gives the reason.

#include <foundation/core/Choice.h>
#include <foundation/core/Link.h>
#include <foundation/core/Ref.h>

namespace {
struct Chain final {};

struct Node {
    using brand_type = Chain;
    int value = 0;
};
}  // namespace

int main() {
    Node node{.value = 3};
    ::foundation::core::Link<Node, Chain> link = ::foundation::core::to(node);
    ::foundation::core::Option<::foundation::core::Ref<Node, Chain>> kept{::foundation::core::none};
    return link.match(
        [&kept](::foundation::core::Ref<Node, Chain> const& object) noexcept {
            kept = ::foundation::core::some(object);
            return object->value;
        },
        [] noexcept { return 0; });
}
