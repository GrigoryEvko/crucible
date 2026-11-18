// W004: a user-space wait grade x Block in the row.
//
// The SpinPause grade of Wait claims that the wait stays out of the
// kernel.  The atom with<Block> puts Block in the row of the binding.  The
// row is the one source of the fact, so the band claims more than the row,
// and the pack trips W004 alone.

#include <fixy/Bands.h>
#include <fixy/Fn.h>

int main() {
    // The size instantiates the class, and a band payload has no default
    // constructor.  So the size and not a default binding is the use, and
    // the refusal of the class body is the only error.
    static_cast<void>(
        sizeof(::fixy::fn<::fixy::wait::SpinPause<int>, ::fixy::atom::with<::foundation::effects::Effect::Block>>));
    return 0;
}
