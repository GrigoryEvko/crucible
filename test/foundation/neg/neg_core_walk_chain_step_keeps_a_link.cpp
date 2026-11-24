// The step of a walk of a chain keeps a Ref of the link that it reads in a
// variable that it captures by reference.  The link is a value of the walk,
// which ends at the next link, so the kept Ref would name a value that no
// longer exists.  The store check of the door (StoresNoBorrowOutside of
// foundation/core/CaptureStore.h) refuses the step.

#include <foundation/core/Choice.h>
#include <foundation/core/Ref.h>
#include <foundation/core/Walk.h>

namespace {

struct Node {
    int value = 0;
    ::foundation::core::Option<::foundation::core::Ref<Node const>> next{::foundation::core::none};
};

using Link = ::foundation::core::Ref<Node const>;

}  // namespace

int main() {
    Node const last{.value = 2, .next = ::foundation::core::none};
    Node const first{
        .value = 1,
        .next = ::foundation::core::Option<Link>::some(::foundation::core::erase_brand(::foundation::core::to(last)))};
    ::foundation::core::Option<::foundation::core::Ref<Link const>> kept{::foundation::core::none};
    return static_cast<int>(
        ::foundation::core::walk_chain(
            ::foundation::core::Option<Link>::some(::foundation::core::erase_brand(::foundation::core::to(first))),
            [&kept](Link const& link) noexcept {
                kept = ::foundation::core::some(::foundation::core::erase_brand(::foundation::core::to(link)));
                return link->next;
            })
            .count());
}
