// The step of a walk of a chain keeps the link that it reads in a variable
// that it captures by reference.  The step runs inside the walk, and the
// kept borrow outlives the call.  The store check of the door
// (StoresNoBorrowOutside of foundation/core/CaptureStore.h) refuses the step.

#include <foundation/core/Choice.h>
#include <foundation/core/Ref.h>
#include <foundation/core/Walk.h>

namespace {

struct NodeBrand final {};

struct Node;

using Link = ::foundation::core::Ref<Node const, NodeBrand>;

struct Node {
    using brand_type = NodeBrand;

    int value = 0;
    ::foundation::core::Option<Link> next{::foundation::core::none};
};

}  // namespace

int main() {
    Node const last{.value = 2, .next = ::foundation::core::none};
    Node const first{.value = 1, .next = ::foundation::core::Option<Link>::some(::foundation::core::to(last))};
    ::foundation::core::Option<Link> kept{::foundation::core::none};
    return static_cast<int>(
        ::foundation::core::walk_chain(::foundation::core::Option<Link>::some(::foundation::core::to(first)),
                                       [&kept](Link const& link) noexcept {
                                           kept = ::foundation::core::some(link);
                                           return link->next;
                                       })
            .count());
}
