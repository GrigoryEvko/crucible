// A copy of a loan key would open a second frame of an owner beside the
// first one.  The key does not copy.

#include <foundation/core/Array.h>

int main() {
    auto [array, key] = ::foundation::core::mint_fixed_array<int, 4>();
    auto second = key;
    key = array.with_mut(std::move(key), [](auto run) noexcept { static_cast<void>(run.size()); });
    second = array.with_mut(std::move(second), [](auto run) noexcept { static_cast<void>(run.size()); });
    return array.at(::foundation::core::index<0>());
}
