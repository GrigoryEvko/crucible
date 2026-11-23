// The checked lifetime start builds an object over bytes, and calls no
// constructor.  A mask over bytes could hold a bit that names no atom, which
// the one checked door of a mask from a word, EffectMask::from_raw, refuses.
// The byte seal of the mask refuses the start.  The positive control is
// from_raw in the check file of foundation/effects/Row.h.

#include <foundation/core/Array.h>
#include <foundation/effects/Row.h>

#include <cstdint>

struct MaskBytes {
    std::uint64_t word = 0xffff;
};

int main() {
    MaskBytes bytes{};
    static_cast<void>(::foundation::lifetime::start_as_array<::foundation::effects::EffectMask>(&bytes, 1));
    return 0;
}
