// A branded borrow has no public constructor, so a brand read off one
// borrow cannot be spelled onto a borrow of another object.  The only
// public constructor of BorrowedRef is constrained to the erased
// brand, and the note shows that constraint answering false.

#include <fixy/Borrowed.h>

int main() {
    int held = 1;
    int other = 2;
    auto genuine = ::fixy::mint_borrowed_ref(held);
    ::fixy::BorrowedRef<int, decltype(genuine)::brand_type> forged{other};
    (void)forged;
    return 0;
}
