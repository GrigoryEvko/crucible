// The owner core moves the state of a SmallList by its bytes, and the inline
// elements live in that state.  A Box owns its object, so a copy of its bytes
// would give the object two owners.  The mint refuses an element that is not
// trivially copyable.

#include <foundation/core/Ref.h>
#include <foundation/core/SmallList.h>

#include <cstddef>

int main() {
    auto const boxes = ::foundation::core::mint_small_list<::foundation::core::Box<int>, 2>(
        ::foundation::core::Quantity<::foundation::core::unit::Element, std::size_t>{8});
    return static_cast<int>(boxes.size());
}
