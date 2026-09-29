// The checked read of a count image needs a context that owns IO.  A test
// scope that claims only its own atom owns no IO, so the row of the
// context, not its capability source, decides the read.

#include <foundation/algebra/lattices/StrongCounterLattice.h>
#include <foundation/effects/Ctx.h>

int main() {
    namespace fe = ::foundation::effects;
    namespace fl = ::foundation::algebra::lattices;
    fe::ExecCtx<fe::Test, fe::Row<fe::Effect::Test>> const scope{fe::testing::test()};
    auto const read =
        fl::GenerationLattice::mint_from_image(scope, fl::GenerationLattice::image_of(fl::GenerationLattice::top()));
    return read.has_value() ? 0 : 1;
}
