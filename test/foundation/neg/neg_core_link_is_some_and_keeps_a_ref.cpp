// The test of is_some_and of a Link keeps the Ref that the Link lends in an
// Option that it captures by reference, as the test of is_some_and of an
// Option does.  The store check refuses the test (LentArmStoresNoBorrow of
// foundation/core/Choice.h).

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
    ::foundation::core::Link<Node, Chain> const link = ::foundation::core::to(node);
    ::foundation::core::Option<::foundation::core::Ref<Node const, Chain>> kept{::foundation::core::none};
    bool const is_odd = link.is_some_and([&kept](::foundation::core::Ref<Node const, Chain> const& object) noexcept {
        kept = ::foundation::core::some(object);
        return object->value % 2 == 1;
    });
    return static_cast<int>(is_odd);
}
