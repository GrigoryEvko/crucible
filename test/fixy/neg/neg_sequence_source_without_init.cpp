// A sequence source is minted only by process startup.  A background
// context owns no Init, so a producer that runs there cannot make a
// second source and give its values any grade it likes.

#include <fixy/Stale.h>
#include <foundation/effects/Ctx.h>

int main() {
    namespace fe = ::foundation::effects;
    fe::ExecCtx<fe::Bg, fe::Row<fe::Effect::Bg, fe::Effect::IO>> const background{fe::testing::bg()};
    fixy::SequenceSource source = fixy::mint_sequence_source(background);
    return static_cast<int>(source.stamp().position());
}
