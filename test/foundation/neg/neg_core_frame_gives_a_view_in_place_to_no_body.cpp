// The body of the in-place frame with_mut of a keyed FixedArray gives a captured
// View of the same array to a function with no body in the unit.  The frame lends
// the elements to the body under a unique borrow, and no walk sees what that
// function reads.  The kind stale-borrow of the rule view-escape refuses the call.

#include <foundation/core/Array.h>
#include <foundation/core/KeyedMint.h>

int read_out_of_line(::foundation::core::View<int const, 4> view) noexcept;

int main() {
    auto [array, key] = ::foundation::core::mint_keyed_owner(::foundation::core::fixed_array_owner<int, 4>);
    auto const& readable = array;
    ::foundation::core::View<int const, 4> const elements = readable.view();
    return array.with_mut(key, [elements](auto out) noexcept {
        for (int& element : out) {
            element = 7;
        }
        return read_out_of_line(elements);
    });
}
