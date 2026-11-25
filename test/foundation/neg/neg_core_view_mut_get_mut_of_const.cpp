// A body asks a const ViewMut for the RefMut of an element.  A const ViewMut
// writes nothing, so it gives no writer of an element.

#include <foundation/core/Array.h>

#include <utility>

int main() {
    auto [array, key] = ::foundation::core::mint_keyed_owner(::foundation::core::fixed_array_owner<int, 4>,
                                                             ::foundation::core::FixedArray<int, 4>{1, 2, 3, 4});
    array.with_mut(key, [](auto out) noexcept {
        auto const& reader = out;
        auto element = std::move(reader.get_mut(0)).expect("one element");
        static_cast<void>(element);
    });
    return 0;
}
