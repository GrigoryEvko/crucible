// A keyed array has no value door of its run either: a fill through another
// name would write the run while a frame of the array is open.

#include <foundation/core/Array.h>

int main() {
    auto [array, key] = ::foundation::core::mint_keyed_owner(::foundation::core::fixed_array_owner<int, 4>);
    array.with_mut(key, [&written = array](auto run) noexcept {
        written.fill(2);
        static_cast<void>(run.size());
    });
    return array.at(::foundation::core::index<0>());
}
