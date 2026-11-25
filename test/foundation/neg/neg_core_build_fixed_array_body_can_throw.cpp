// A body of build_fixed_array that can throw.  The compiler orders the refused
// twin of the door below the door, so the twin binds to this body and gives the
// reason.

#include <foundation/core/Array.h>

int main() {
    auto const array = ::foundation::core::build_fixed_array<int, 4>([](auto run) { static_cast<void>(run.size()); });
    return array.at(::foundation::core::index<0>());
}
