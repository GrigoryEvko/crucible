// The body of the in-place frame door with_mut of a keyed FixedArray keeps the
// View of its ViewMut in a variable that it captures by reference.  The View
// outlives the frame, so a read through it sees the elements while a second
// frame of the array writes them.  The store twin of the door binds to the
// body, so the refusal gives the reason of the store check.

#include <foundation/core/Array.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Region.h>

int main() {
    ::foundation::core::Option<::foundation::core::View<int const>> kept{::foundation::core::none};
    auto [array, key] = ::foundation::core::mint_keyed_owner(::foundation::core::fixed_array_owner<int, 4>);
    array.with_mut(key, [&kept](auto run) noexcept {
        kept = ::foundation::core::Option<::foundation::core::View<int const>>::some(run.view());
    });
    return kept.is_some() ? 0 : 1;
}
