// A function stores the borrow of a local Handle into the object of a
// parameter by reference.  The borrow states refers{}, so the store check of
// the rule view-escape reads it as a borrow of the Handle and refuses the
// store.

#include <fixy/Core.h>
#include <fixy/os/Fs.h>

namespace fs = ::fixy::fs;

namespace {

struct KeptBorrow {
    ::fixy::Option<fs::DirectoryRef<fs::directory_rights>> borrow{};
};

void keep_borrow_of_local(KeptBorrow& kept) noexcept {
    fs::DirectoryHandle owner{};
    kept.borrow = ::fixy::some(owner.ref());
}

}  // namespace

int main() {
    KeptBorrow kept{};
    keep_borrow_of_local(kept);
    return kept.borrow.is_some() ? 1 : 0;
}
