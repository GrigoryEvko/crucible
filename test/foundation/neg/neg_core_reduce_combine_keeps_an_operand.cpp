// The combine of a reduction by a tree keeps a Ref of an operand in a
// variable that it captures by reference.  An operand can be a partial value
// of the tree, which ends in the door, so the kept Ref would name an object
// that no longer exists.  The store check of the combine
// (StoresNoBorrowOutside of foundation/core/CaptureStore.h) refuses it.

#include <foundation/core/Array.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Reduce.h>
#include <foundation/core/Ref.h>

int main() {
    ::foundation::core::FixedArray<int, 4> const values{1, 2, 3, 4};
    ::foundation::core::Option<::foundation::core::Ref<int const>> kept{::foundation::core::none};
    ::foundation::core::Option<int> const total =
        ::foundation::core::reduce_by_tree(values.view(), [&kept](int const& left, int const& right) noexcept {
            kept = ::foundation::core::some(::foundation::core::erase_brand(::foundation::core::to(left)));
            return left + right;
        });
    return total.is_some() ? 0 : 1;
}
