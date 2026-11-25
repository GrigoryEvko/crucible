// Negative-compile test.  This file must fail to compile.
//
// The key of a BackgroundThread makes a thread and its first handle.  Its
// constructor is private, and its friends are the frame door and the holder, so
// no other code builds a key.

#include <crucible/BackgroundThread.h>

int main() {
    const crucible::BackgroundThreadKey key{};
    static_cast<void>(sizeof(key));
    return 0;
}
