// The body of a strided walk keeps a Ref of an element in an Option that it
// captures by reference.  A walk of a unique view would then leave a second
// road to an element after the call.  The store check of the walk refuses
// the body (StoresNoBorrowOutside of foundation/core/CaptureStore.h).

#include <foundation/core/Array.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Ref.h>
#include <foundation/core/Strided.h>

#include <cstddef>

int main() {
    ::foundation::core::FixedArray<int, 6> const values{1, 2, 3, 4, 5, 6};
    ::foundation::core::FixedArray<std::size_t, 2> const sizes{2, 3};
    ::foundation::core::FixedArray<std::size_t, 2> const strides{3, 1};
    auto const view =
        ::foundation::core::mint_strided_view(values.view(), sizes.view(), strides.view(), 0).expect("it fits");
    ::foundation::core::Option<::foundation::core::Ref<int const>> kept{::foundation::core::none};
    auto const walked = ::foundation::core::walk(
        [&kept](int const& value) noexcept {
            kept = ::foundation::core::some(::foundation::core::erase_brand(::foundation::core::to(value)));
        },
        view);
    return static_cast<int>(walked.is_ok());
}
