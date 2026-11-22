// Negative compile test.  This file must fail to compile.
//
// Cipher::content_addressed takes a Ref of the region, and a Ref is never
// null.  A null pointer does not convert to a Ref, so no store gets the
// payload of a null region through this door.  The empty payload has its own
// door: cipher::content_addressed_payload with no value.

#include <crucible/Cipher.h>

int main() {
    const auto payload = ::crucible::Cipher::content_addressed(nullptr);
    return static_cast<int>(static_cast<bool>(payload));
}
