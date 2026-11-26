// The body of the in-place frame with_mut of a keyed FixedArray captures a View
// of the same array.  The frame lends the elements to the body under a unique
// borrow, so a read of the View aliases the writes of the run.  The kind
// stale-borrow of the rule view-escape refuses the call.

#include <foundation/core/Array.h>
#include <foundation/core/KeyedMint.h>

int main() {
    auto [array, key] = ::foundation::core::mint_keyed_owner(::foundation::core::fixed_array_owner<int, 4>);
    auto const& readable = array;
    ::foundation::core::View<int const, 4> const elements = readable.view();
    return array.with_mut(key, [elements](auto out) noexcept {
        for (int& element : out) {
            element = 7;
        }
        return elements.at(::foundation::core::index<0>());
    });
}
