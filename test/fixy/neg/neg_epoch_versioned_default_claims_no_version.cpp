// A value that was never produced at a version has no version to report.
// A default of the genesis version would let a gate that asks for the
// genesis version admit a value that nothing produced.  The default
// constructor is deleted, so the program must state the version or call
// at_genesis().

#include <fixy/EpochVersioned.h>

int main() {
    fixy::EpochVersioned<int> const unproduced{};
    return unproduced.peek();
}
