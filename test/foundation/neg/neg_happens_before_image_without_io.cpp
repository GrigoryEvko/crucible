// The checked read of a clock image needs a context that owns IO.  A test
// scope that claims only its own atom owns no IO, so it cannot read a
// clock from bytes.

#include <foundation/algebra/lattices/HappensBefore.h>
#include <foundation/effects/Ctx.h>

int main() {
    namespace fe = ::foundation::effects;
    namespace fl = ::foundation::algebra::lattices;
    using HB = fl::HappensBeforeLattice<2>;
    fe::ExecCtx<fe::Test, fe::Row<fe::Effect::Test>> const scope{fe::testing::test()};
    auto const read = HB::mint_from_image(scope, HB::image_of(HB::bottom()));
    return read.has_value() ? 0 : 1;
}
