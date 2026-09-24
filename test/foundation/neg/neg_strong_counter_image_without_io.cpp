// The checked read of a count image needs a context that owns IO.  The
// foreground context claims nothing, so a count cannot be read from bytes
// on the dispatch path.

#include <foundation/algebra/lattices/StrongCounterLattice.h>
#include <foundation/effects/Ctx.h>

int main() {
    namespace fl = ::foundation::algebra::lattices;
    ::foundation::effects::ExecCtx<> const foreground = ::foundation::effects::testing::foreground();
    auto const read = fl::EpochLattice::mint_from_image(foreground, fl::EpochLattice::image_of(fl::EpochLattice::top()));
    return read.has_value() ? 0 : 1;
}
