// The compile-time checks of foundation/reflect/Mix.h.

#include <foundation/reflect/Mix.h>

#include <cstdint>

namespace foundation::reflect::detail::mix_checks {

// These literals pin the bits of the one mixer.  The ids of Hash.h, the
// content hashes of the product and the keys of the Cipher fold through it,
// so a change of a constant or a shift moves each stored key.  The values
// come from the MurmurHash3 finalizer and the step of combine_ids, computed
// outside the compiler.
static_assert(fmix64(0) == 0);
static_assert(fmix64(1) == 0xb456bcfc34c2cb2cULL);
static_assert(fmix64(0xdeadbeefcafebabeULL) == 0x7082995008f0c48cULL);
static_assert(fmix64(0xffffffffffffffffULL) == 0x64b5720b4b825f21ULL);
static_assert(combine_ids(0, 0) == 0x9ca066f1a4ab2eeaULL);
static_assert(combine_ids(1, 2) == 0xf9122d6051144cc9ULL);
static_assert(combine_ids(2, 1) == 0x4c5ec34062e859d9ULL);
static_assert(combine_ids(12345, 678) == 0x6c1e8cb2cd5a4237ULL);

// A step of a fold displaces its input, so the state 0 and the input 0 give
// no 0, and the two operands do not change places.
static_assert(combine_ids(0, 0) != 0 && combine_ids(1, 2) != combine_ids(2, 1));

}  // namespace foundation::reflect::detail::mix_checks
