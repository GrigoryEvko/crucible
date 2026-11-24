// A maker holds a value of its own.  The mint takes each input of the owner
// through its list of arguments, so the call site shows what the owner holds.
// A maker with state would hide an input, so the gate of the mint refuses
// each maker that is not empty.

#include <foundation/core/Array.h>

namespace {

struct FilledArrayOwner final {
    int fill = 0;

    template <::foundation::core::IsKeyBrand Brand>
    [[nodiscard]] constexpr ::foundation::core::FixedArray<int, 4, Brand>
    operator()(::foundation::core::MintedBrand<Brand> /*proof*/) const noexcept {
        return ::foundation::core::FixedArray<int, 4, Brand>{fill, fill, fill, fill};
    }
};

}  // namespace

int main() {
    auto [array, key] = ::foundation::core::mint_keyed_owner(FilledArrayOwner{7});
    static_cast<void>(key);
    return array.at(::foundation::core::index<0>());
}
