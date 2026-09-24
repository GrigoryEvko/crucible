// PublishOnce<T> adds the star itself and hands off a T*, so T is the
// pointee.  PublishOnce<Foo*> builds an atomic<Foo**> and publishes the
// address of a pointer variable — usually a local — rather than the
// handle the caller meant, and every observer then reads a live pointer
// through a dangling one.
//
// This fixture is the standing witness that the guard is not
// tautological.  The guard this replaced read
// `is_pointer_v<T*> || is_same_v<T, T>`, whose second disjunct is true
// for every T: it admitted this exact instantiation and enforced
// nothing.  A guard that went tautological again would make this
// fixture stop failing.
//
// PublishOnce<Payload> compiles, so the pointee is what is refused, not
// the type.

#include <fixy/handle/PublishOnce.h>

namespace h = fixy::handle;

namespace {
struct Payload {
    int value = 0;
};
}  // namespace

// The instantiation happens inside sizeof rather than through an alias,
// so the refusal is reported once at one line.
int main() { return sizeof(h::PublishOnce<Payload*>) == 0 ? 1 : 0; }
