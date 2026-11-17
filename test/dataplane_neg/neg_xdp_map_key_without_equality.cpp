// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A map image finds a key by its equality.  A key with no equality would
// match by accident of its bytes, so the image refuses it.

#include <crucible/cntp/dataplane/Xdp.h>

struct Key {
    std::uint32_t value = 0;
};

struct Value {
    std::uint32_t value = 0;
};

int main() { return sizeof(crucible::cntp::dataplane::BpfMapImage<Key, Value, 4>) == 0 ? 0 : 1; }
