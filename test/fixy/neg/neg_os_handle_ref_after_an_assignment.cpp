// A borrow of a Handle is read after a move assignment over the Handle.  The
// assignment closes the descriptor of the borrow, so the Handle ends each
// borrow of its number (owns_by_number{}), and the rule view-escape refuses
// the read.

#include <fixy/os/Fs.h>

namespace fs = ::fixy::fs;

int main() {
    fs::DirectoryHandle owner{};
    auto const borrow = owner.ref();
    owner = fs::DirectoryHandle{};
    return borrow.is_open() ? 1 : 0;
}
