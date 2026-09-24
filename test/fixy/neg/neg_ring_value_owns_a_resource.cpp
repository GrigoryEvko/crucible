// A ring cell is overwritten in place and never destroyed, and the copy
// into and out of it is a plain byte copy under no lock.  A value that
// owns anything therefore leaks once per slot per lap, and its copy
// would run against a cell the other side may already be reading.
//
// The capacity is a power of two and non-zero, so only the value
// clause can reject this.

#include <fixy/concurrent/MpscRing.h>

namespace c = fixy::concurrent;

namespace {
// Trivially copyable, but NOT trivially destructible: the destructor
// frees, and the ring never runs it.
struct Owns {
    int* owned = nullptr;
    ~Owns() { delete owned; }
};
}  // namespace

// The instantiation happens inside sizeof rather than through an alias
// declaration.  An alias would be rejected at its own line and then
// again at every use of the alias name, and a fixture that reports
// three errors cannot say which one it tested.
int main() { return sizeof(c::MpscRing<Owns, 8>) == 0 ? 1 : 0; }
