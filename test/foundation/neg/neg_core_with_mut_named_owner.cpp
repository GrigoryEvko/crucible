// A frame door gets a named owner.  The caller would keep the name while the
// body runs, so the body could take a second borrow of the owner through a
// capture beside its ViewMut.  The door takes the owner only by move.

#include <foundation/core/Array.h>

int main() {
    ::foundation::core::FixedArray<int, 4> array{1, 2, 3, 4};
    static_cast<void>(::foundation::core::with_mut(array, [](auto out) noexcept { static_cast<void>(out.size()); }));
    return 0;
}
