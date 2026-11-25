// A body of a keyed frame door that can throw.  The compiler orders the refused
// twin of the door below the door, so the twin binds to this body and gives the
// reason.

#include <foundation/core/Array.h>

int main() {
    auto [array, key] = ::foundation::core::mint_keyed_owner(::foundation::core::fixed_array_owner<int, 4>);
    array.with_mut(key, [](auto run) { static_cast<void>(run.size()); });
    return array.at(::foundation::core::index<0>());
}
