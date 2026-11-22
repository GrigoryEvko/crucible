// A caller names the brand of mint_fixed_array.  A key of a brand that the
// caller holds would open the frames of each array of that brand, so the pack
// of the mint takes each explicit argument after T and N, and the gate of the
// mint refuses it.

#include <foundation/core/Array.h>

namespace {

struct HeldBrand final {};

}  // namespace

int main() {
    auto [array, key] = ::foundation::core::mint_fixed_array<int, 4, HeldBrand>();
    key = array.with_mut(std::move(key), [](auto run) noexcept { static_cast<void>(run.size()); });
    return array.at(::foundation::core::index<0>());
}
