// Negative-compile test.  This file must fail to compile.
//
// The read of the generation of a storage takes the address of a
// c10::StorageImpl that a C caller gives.  It reads the object only under the
// proof that the calling thread holds the GIL, so a call with the address
// alone finds no door.

#include "../../vessel/torch/gil_held.h"

int main() { return crucible::vessel::storage_generation_at(nullptr).raw() == 0U ? 0 : 1; }
