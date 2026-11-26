// A borrow of a Handle is read after release_to_abi(), which gives the
// descriptor to an ABI and advances the generation of its number.  The door
// takes the Handle by an rvalue reference, so it ends each borrow of the
// number, and the rule view-escape refuses the read.

#include <fixy/os/Fs.h>

#include <utility>

namespace fs = ::fixy::fs;

int main() {
    fs::DirectoryHandle owner{};
    auto const borrow = owner.ref();
    int const number = std::move(owner).release_to_abi();
    return borrow.is_open() ? number : 0;
}
