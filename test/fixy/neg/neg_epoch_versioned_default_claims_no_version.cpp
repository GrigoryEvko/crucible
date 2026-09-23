// A value that was never produced at a version has no version to report.
// The old wrapper defaulted to the genesis version, and a gate that asked
// for the genesis version then admitted a value that nothing had
// produced.  The default constructor is deleted, so the program must
// state the version or call at_genesis().

#include <fixy/EpochVersioned.h>

int main() {
    fixy::EpochVersioned<int> const unproduced{};
    return unproduced.peek();
}
