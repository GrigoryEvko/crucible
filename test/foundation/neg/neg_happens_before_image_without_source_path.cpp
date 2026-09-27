// The checked read of a clock image needs a tag with a source path.  A
// tag in an unnamed namespace has none: each translation unit has its own
// such tag under one name, and an image of one can read back as a clock
// of another.  A context that owns IO does not help.

#include <foundation/algebra/lattices/HappensBefore.h>
#include <foundation/effects/Ctx.h>

namespace {
struct HiddenClock {};
}  // namespace

int main() {
    namespace fe = ::foundation::effects;
    namespace fl = ::foundation::algebra::lattices;
    using HB = fl::HappensBeforeLattice<2, HiddenClock>;
    fe::ExecCtx<fe::Test, fe::Row<fe::Effect::Test, fe::Effect::IO>> const scope{fe::testing::test()};
    HB::image_type const image{};
    auto const read = HB::mint_from_image(scope, image);
    return read.has_value() ? 0 : 1;
}
