#pragma once

// The foreground context of the runtime, branded by the Vigil whose
// producer claim mints it.  The view gates of the replay chain ask for
// this brand, so a thread that holds the claim of any other state passes
// none of them.
//
// The proof of the record path comes from the same claim.  The trace ring
// and the metadata log of a Vigil take it to publish their heads with no
// check load (THE PROOF OF THE ONE WRITER of foundation/SoleWriterProof.h).

#include <foundation/SoleWriterProof.h>
#include <foundation/effects/Ctx.h>

namespace crucible {

class Vigil;

using VigilFgCtx =
    ::foundation::effects::ExecCtx<::foundation::effects::ctx_cap::BrandedFg<Vigil>, ::foundation::effects::Row<>>;

// The role of the producer of the record path: the writer of the head of the
// trace ring and of the head of the metadata log.
struct RecordProducerRole final {};

// The proof that its holder is the thread that holds the producer claim of
// the Vigils.  Only that claim makes one, from the context that it minted
// (ProducerClaim::prove_sole_writer), and only the Vigil asks it.
using RecordProof =
    ::foundation::SoleWriterProof<::foundation::effects::host::ProducerClaim<Vigil>, RecordProducerRole>;

}  // namespace crucible
