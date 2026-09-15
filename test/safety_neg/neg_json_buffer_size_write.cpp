// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// oob-62 fixture: fixed_json_buffer's size is not writable from outside.
//
// `data`, `size` and `ok` were public members of an aggregate, so this
// assignment compiled and BOTH writers then went out of bounds:
// push()'s `size == Capacity` guard is false for size > Capacity, and
// append()'s `Capacity - size` underflows to a bound near 2^64.
// Reproduced under ASan as a 1-byte write and a 10-byte memcpy past the
// array.  The members are private now, so the bad value is refused at
// the assignment rather than one call later at the use.

#include <crucible/safety/diag/JsonEmitter.h>

#include <cstddef>

int main() {
    crucible::safety::diag::detail::fixed_json_buffer<16> buf;
    buf.size = std::size_t{999};
    return 0;
}
