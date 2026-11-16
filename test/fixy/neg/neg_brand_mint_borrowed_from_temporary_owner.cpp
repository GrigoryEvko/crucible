// A borrow of the run of a temporary owner of the base dangles at the end
// of the statement, because the run ends with the owner.  An rvalue owner
// selects the deleted twin of mint_borrowed, so the call names a deleted
// function.  A View or a TextView as an rvalue borrows its run, and the
// test of the header admits it.

#include <fixy/Borrowed.h>
#include <fixy/Core.h>

namespace {
struct Owner {};
}  // namespace

int main() {
    auto borrow = ::fixy::mint_borrowed<Owner>(::fixy::FixedArray<int, 3>{});
    (void)borrow;
    return 0;
}
