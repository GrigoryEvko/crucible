// A body fills a named ViewMut.  The fill of a ViewMut takes it by value, as
// copy and write_each do, so a named ViewMut would need a copy, and a copy
// would be a second writer of the run.

#include <foundation/core/Array.h>

int main() {
    ::foundation::core::FixedArray<int, 4> array{1, 2, 3, 4};
    array.with_mut([](auto out) noexcept { ::foundation::core::fill(out, 0); });
    return 0;
}
