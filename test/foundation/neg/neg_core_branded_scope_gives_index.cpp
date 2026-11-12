// An index of a branded scope proves nothing outside the scope, so the mint
// refuses a body whose result holds the brand.

#include <foundation/core/Branded.h>

int main() {
    ::foundation::core::FixedArray<int, 4> values{1, 2, 3, 4};
    auto escaped =
        ::foundation::core::mint_branded_scope(values.view(), [](auto view) noexcept { return view.index(0); });
    return escaped.is_some() ? 1 : 0;
}
