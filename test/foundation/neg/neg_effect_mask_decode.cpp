// The base door decode builds a value from bytes, and calls no constructor.
// A mask from a word could hold a bit that names no atom, which the one
// checked door of a mask from a word, EffectMask::from_raw, refuses, and
// raw() would give the bit out again.  The byte seal of the mask refuses
// decode.  The positive control is from_raw in the check file of
// foundation/effects/Row.h.

#include <foundation/core/Scalar.h>
#include <foundation/effects/Row.h>

int main() {
    using Mask = ::foundation::effects::EffectMask;
    return ::foundation::core::decode<Mask>(Mask::underlying_type{0xffff}).is_some() ? 0 : 1;
}
