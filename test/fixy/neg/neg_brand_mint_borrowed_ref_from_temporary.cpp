// A borrow of a temporary dangles at the end of the statement.  The
// forwarding-reference twin of mint_borrowed_ref is the better match
// for the prvalue and is deleted, so the call names a deleted function.

#include <fixy/Borrowed.h>

int main() {
    auto borrow = ::fixy::mint_borrowed_ref(42);
    (void)borrow;
    return 0;
}
