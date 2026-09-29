// Tier 1: the payload has to be an object type a wrapper can hold by
// value.  void has no storage.
//
// This fixture is here for the tier chain rather than for the payload
// rule: it holds tier 1 to ONE diagnostic.  fn declares accessors that
// return a reference to the payload and a constructor that takes it, and
// a member's type is instantiated with the class whatever the assertion
// concluded.  Over `void`, those members would add errors about forming
// a reference to void and an invalid parameter type.  fn declares them
// over a stand-in that is Type for every payload tier 1 admits, which
// leaves the tier-1 message alone.

#include <fixy/Fn.h>

int main() {
    [[maybe_unused]] ::fixy::fn<void> refused{};
    return 0;
}
