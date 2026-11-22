// The element door with no key on a keyed FixedArray.  A keyed array lends an
// element that a body writes only with its key.

#include <foundation/core/Array.h>

int main() {
    auto [array, key] = ::foundation::core::mint_fixed_array<int, 4>();
    array.with_element(::foundation::core::index<1>(), [](int& element) noexcept { element = 1; });
    key = array.with_mut(std::move(key), [](auto run) noexcept { static_cast<void>(run.size()); });
    return array.at(::foundation::core::index<1>());
}
