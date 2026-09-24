// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// PublishSlot<T> adds the star itself and hands off a T*, as PublishOnce
// does, so T is the pointee.  PublishSlot<Foo*> builds an atomic<Foo**>
// and publishes the address of a pointer variable rather than the handle
// the caller meant.  The pointee check in the class body of PublishSlot
// is what refuses it, apart from the one in PublishOnce.
//
// Expected diagnostic: the static_assert in PublishSlot that asks for the
// pointee.

#include <fixy/handle/PublishOnce.h>

namespace h = fixy::handle;

namespace {
struct Payload {
    int value = 0;
};
}  // namespace

// The instantiation happens inside sizeof rather than through an alias,
// so the refusal is reported once at one line.
int main() { return sizeof(h::PublishSlot<Payload*>) == 0 ? 1 : 0; }
