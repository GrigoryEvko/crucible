#pragma once

// The foreground context of the runtime, branded by the Vigil whose
// producer claim mints it.  The view gates of the replay chain ask for
// this brand, so a thread that holds the claim of any other state passes
// none of them.

#include <foundation/effects/Ctx.h>

namespace crucible {

class Vigil;

using VigilFgCtx =
    ::foundation::effects::ExecCtx<::foundation::effects::ctx_cap::BrandedFg<Vigil>, ::foundation::effects::Row<>>;

}  // namespace crucible
