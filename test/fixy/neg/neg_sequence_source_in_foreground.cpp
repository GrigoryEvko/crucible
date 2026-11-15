// A sequence source is minted only by process startup.  The foreground
// context claims nothing, so the dispatch path cannot make a source and
// grade a value with it.

#include <fixy/Stale.h>
#include <foundation/effects/Ctx.h>

int main() {
    ::foundation::effects::ExecCtx<> const foreground = ::foundation::effects::testing::foreground();
    fixy::SequenceSource source = fixy::mint_sequence_source(foreground);
    return static_cast<int>(source.stamp().position());
}
