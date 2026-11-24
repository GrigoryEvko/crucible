// A caller names the brand of mint_keyed_owner.  A key of a brand that the
// caller holds would open the frames of each owner of that brand, so the pack
// of the mint takes each explicit argument, and the gate of the mint refuses
// it.

#include <foundation/core/Array.h>

#include <utility>

namespace {

struct HeldBrand final {};

}  // namespace

int main() {
    auto [array, key] = ::foundation::core::mint_keyed_owner<HeldBrand>(::foundation::core::fixed_array_owner<int, 4>);
    key = array.with_mut(std::move(key), [](auto run) noexcept { static_cast<void>(run.size()); });
    return array.at(::foundation::core::index<0>());
}
