// update gives the body a copy of the element, so the array holds a whole
// element while the body runs.  A move would leave an empty element for a read
// of the array inside the body, so an element with no copy has no update.

#include <foundation/core/Array.h>

int main() {
    ::foundation::core::FixedArray<::foundation::core::HeapArray<int>, 2> runs{};
    runs.update(::foundation::core::index<0>(),
                [](::foundation::core::HeapArray<int>& run) noexcept { static_cast<void>(run.size()); });
    return 0;
}
