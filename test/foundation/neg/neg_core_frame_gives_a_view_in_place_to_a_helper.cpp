// The body of the in-place frame with_mut of a keyed FixedArray gives a captured
// View of the same array to a helper that reads it.  The frame lends the elements
// to the body under a unique borrow, so the helper reads under the writes of the
// run.  The kind stale-borrow of the rule view-escape refuses the call.

#include <foundation/core/Array.h>
#include <foundation/core/KeyedMint.h>

namespace {

int sum_of(::foundation::core::View<int const, 4> view) noexcept {
    int sum = 0;
    for (int const element : view) {
        sum += element;
    }
    return sum;
}

}  // namespace

int main() {
    auto [array, key] = ::foundation::core::mint_keyed_owner(::foundation::core::fixed_array_owner<int, 4>);
    auto const& readable = array;
    ::foundation::core::View<int const, 4> const elements = readable.view();
    return array.with_mut(key, [elements](auto out) noexcept {
        for (int& element : out) {
            element = 7;
        }
        return sum_of(elements);
    });
}
