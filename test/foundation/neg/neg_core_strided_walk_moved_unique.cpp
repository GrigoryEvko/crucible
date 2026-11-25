// The walk that writes through a unique strided view borrows the named view
// for the call, and the caller keeps it.  A unique view given with
// std::move is no borrow of that writer, so the walk refuses it.

#include <foundation/core/Array.h>
#include <foundation/core/Strided.h>

#include <cstddef>
#include <utility>

int main() {
    auto [values, values_key] = ::foundation::core::mint_keyed_owner(
        ::foundation::core::fixed_array_owner<int, 6>, ::foundation::core::FixedArray<int, 6>{1, 2, 3, 4, 5, 6});
    ::foundation::core::FixedArray<std::size_t, 2> const sizes{2, 3};
    ::foundation::core::FixedArray<std::size_t, 2> const strides{3, 1};
    auto const is_walked = values.with_mut(values_key, [&](auto run) noexcept -> bool {
        auto unique = ::foundation::core::mint_strided_view_mut(std::move(run), sizes.view(), strides.view(), 0)
                          .expect("a dense matrix has no overlap");
        return ::foundation::core::walk([](int& element) noexcept { element = 0; }, std::move(unique)).is_ok();
    });
    return static_cast<int>(is_walked);
}
