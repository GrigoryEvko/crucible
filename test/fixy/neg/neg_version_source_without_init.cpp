// A version source is minted only by process startup.  A background
// context owns no Init, so a producer that runs there cannot make a
// second source and vouch for any version it likes.

#include <fixy/EpochVersioned.h>
#include <foundation/effects/Ctx.h>

int main() {
    namespace fe = ::foundation::effects;
    fe::ExecCtx<fe::Bg, fe::Row<fe::Effect::Bg, fe::Effect::IO>> const background{fe::testing::bg()};
    fixy::VersionSource source = fixy::mint_version_source(background);
    return static_cast<int>(source.stamp().epoch().raw());
}
