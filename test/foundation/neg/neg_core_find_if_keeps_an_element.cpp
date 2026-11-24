// The predicate of find_if keeps a Ref of an element in an Option that it
// captures by reference.  The caller reads the Ref after the scan, with no
// View of the scan to name the element.  The store check of the predicate
// refuses it (StoresNoBorrowOutside of foundation/core/CaptureStore.h).

#include <foundation/core/Algorithms.h>
#include <foundation/core/Array.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Ref.h>

int main() {
    ::foundation::core::FixedArray<int, 3> const values{1, 2, 3};
    ::foundation::core::Option<::foundation::core::Ref<int const>> kept{::foundation::core::none};
    auto const found = ::foundation::core::find_if(values.view(), [&kept](int const& value) noexcept {
        kept = ::foundation::core::some(::foundation::core::erase_brand(::foundation::core::to(value)));
        return value == 2;
    });
    return static_cast<int>(found.is_some());
}
