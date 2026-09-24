// A version source is minted only by process startup.  The foreground
// context claims nothing, so the dispatch path cannot make a source.

#include <fixy/EpochVersioned.h>
#include <foundation/effects/Ctx.h>

int main() {
    ::foundation::effects::ExecCtx<> const foreground = ::foundation::effects::testing::foreground();
    fixy::VersionSource source = fixy::mint_version_source(foreground);
    return static_cast<int>(source.stamp().epoch().raw());
}
