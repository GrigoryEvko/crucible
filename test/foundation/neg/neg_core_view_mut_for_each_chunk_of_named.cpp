// A body gives the parts of a named ViewMut to a body.  The parts would write
// the run while the name stays with the body, so the run would have two
// writers.  for_each_chunk takes its ViewMut by move.

#include <foundation/core/Array.h>

int main() {
    auto [array, key] = ::foundation::core::mint_keyed_owner(::foundation::core::fixed_array_owner<int, 8>);
    array.with_mut(key, [](auto out) noexcept {
        auto rest = ::foundation::core::for_each_chunk<4>(out, [](auto part) noexcept { static_cast<void>(part); });
        static_cast<void>(rest);
    });
    return 0;
}
