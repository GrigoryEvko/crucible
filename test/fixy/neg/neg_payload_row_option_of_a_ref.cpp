// A channel payload that holds an Option of a Ref is refused.  An Option
// holds its payload by value, so the walk reads the payload, and a Ref is a
// borrow of an object that no part of the message owns.  The family of a Ref
// is on no roster, so the walk refuses it where it reaches it, and the
// diagnostic names the Ref.  The control is an Option of a Box in
// test_payload_row.cpp, which carries the row of the object of the Box.
//
// VIOLATION: a TU asks for the row of a message that holds an Option of a
// borrow.

#include <fixy/concurrent/PayloadRow.h>

namespace {

// The brand of the borrowed object.
struct BorrowBrand {};

struct HoldsOptionalBorrow {
    int tag = 0;
    ::foundation::core::Option<::foundation::core::Ref<int, BorrowBrand>> borrowed{};
};

// Declared and never used, as in the sibling fixtures.
using forged = ::fixy::concurrent::payload_row_t<HoldsOptionalBorrow>;

}  // namespace

int main() { return 0; }
