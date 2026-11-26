// The body of the element frame with_element of a keyed FixedArray captures a
// View of the same array.  The frame gives the element to the body by a
// reference, so a read of the View aliases the write of the element.  The kind
// stale-borrow of the rule view-escape refuses the call.

#include <foundation/core/Array.h>
#include <foundation/core/KeyedMint.h>

int main() {
    auto [array, key] = ::foundation::core::mint_keyed_owner(::foundation::core::fixed_array_owner<int, 4>);
    auto const& readable = array;
    ::foundation::core::View<int const, 4> const elements = readable.view();
    return array.with_element(key, ::foundation::core::index<0>(), [elements](int& element) noexcept {
        element = 7;
        return elements.at(::foundation::core::index<0>());
    });
}
