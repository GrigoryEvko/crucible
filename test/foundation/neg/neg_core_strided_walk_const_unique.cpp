// A const reference to a unique strided view reads its elements, and it
// writes none of them.  The walk that writes through a unique view refuses
// a const unique view, so the body of the walk gets no writer.

#include <foundation/core/Array.h>
#include <foundation/core/Strided.h>

#include <cstddef>
#include <utility>

int main() {
    ::foundation::core::FixedArray<int, 6> values{1, 2, 3, 4, 5, 6};
    ::foundation::core::FixedArray<std::size_t, 2> const sizes{2, 3};
    ::foundation::core::FixedArray<std::size_t, 2> const strides{3, 1};
    auto const is_walked = values.with_mut([&](auto run) noexcept -> bool {
        auto const unique = ::foundation::core::mint_strided_view_mut(std::move(run), sizes.view(), strides.view(), 0)
                                .expect("a dense matrix has no overlap");
        return ::foundation::core::walk([](int& element) noexcept { element = 0; }, unique).is_ok();
    });
    return static_cast<int>(is_walked);
}
