// The maker fixed_array_owner value-initializes each element, so an element
// with no default constructor has no keyed array, and the gate of the mint
// refuses the maker.

#include <foundation/core/Array.h>

namespace {

struct NoDefault final {
    int value;
    explicit NoDefault(int given) noexcept : value{given} {}
};

}  // namespace

int main() {
    auto [array, key] = ::foundation::core::mint_keyed_owner(::foundation::core::fixed_array_owner<NoDefault, 4>);
    static_cast<void>(key);
    return array.at(::foundation::core::index<0>()).value;
}
