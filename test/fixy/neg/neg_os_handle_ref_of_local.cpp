// A function returns the borrow of a local Handle.  The borrow holds the
// number of the descriptor by value, and it states refers{}, so the rule
// view-escape reads it as a borrow of the Handle and refuses the return.

#include <fixy/os/Fs.h>

namespace fs = ::fixy::fs;

namespace {

fs::DirectoryRef<fs::directory_rights> borrow_of_local() noexcept {
    fs::DirectoryHandle owner{};
    return owner.ref();
}

}  // namespace

int main() { return borrow_of_local().is_open() ? 1 : 0; }
