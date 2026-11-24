// The body of the in-place frame door with_mut of a FixedArray keeps the
// View of its ViewMut in a variable that it captures by reference.  The View
// outlives the frame, so a read through it sees the elements while a second
// frame of the array writes them.  The store twin of the door binds to the
// body, so the refusal gives the reason of the store check, and not the
// reason of the const twin.

#include <foundation/core/Array.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Region.h>

int main() {
    ::foundation::core::Option<::foundation::core::View<int const>> kept{::foundation::core::none};
    ::foundation::core::FixedArray<int, 4> array{};
    array.with_mut([&kept](auto run) noexcept {
        kept = ::foundation::core::Option<::foundation::core::View<int const>>::some(run.view());
    });
    return kept.is_some() ? 0 : 1;
}
