// The pack of the mint refuses each explicit template argument, so no caller
// names a brand that it already holds and opens a second scope of it.

#include <foundation/core/Branded.h>

int main() {
    ::foundation::core::FixedArray<int, 4> values{1, 2, 3, 4};
    return static_cast<int>(
        ::foundation::core::mint_branded_scope<int>(values.view(), [](auto view) noexcept { return view.size(); }));
}
