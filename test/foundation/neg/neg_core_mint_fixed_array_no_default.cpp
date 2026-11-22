// mint_fixed_array value-initializes each element, so an element with no
// default constructor has no mint.

#include <foundation/core/Array.h>

namespace {

struct NoDefault final {
    int value;
    explicit NoDefault(int given) noexcept : value{given} {}
};

}  // namespace

int main() {
    auto [array, key] = ::foundation::core::mint_fixed_array<NoDefault, 4>();
    static_cast<void>(key);
    return array.at(::foundation::core::index<0>()).value;
}
