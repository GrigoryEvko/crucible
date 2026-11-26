// Negative-compile test.  This file must fail to compile.
//
// A state of one producer of its own holds the claim of its own brand, and
// asks that claim for a proof in the role of the record path.  The proof names
// the claim of that brand, and the producer side of the metadata log of a
// Vigil opens only with the proof of the claim of the Vigils.  A second claim
// gives no proof of the side, so the thread of another brand cannot publish
// the head while the Vigil records.

#include <crucible/MetaLog.h>
#include <crucible/Vigil.h>
#include <fixy/concurrent/SpscEnds.h>

namespace neg_fixture {

class Stranger {
public:
    explicit Stranger(::foundation::effects::Alloc allocation) noexcept
        : log_{::fixy::concurrent::mint_held_spsc_split<crucible::MetaLog, crucible::VigilLogSides>(allocation,
                                                                                                    allocation)} {}

    crucible::MetaIndex append(::fixy::View<const crucible::TensorMeta> metas) noexcept {
        auto const context = claim_.mint_producer_context();
        return log_.producer_side().with_end(
            claim_.prove_sole_writer<crucible::RecordProducerRole>(context),
            [run = crucible::mint_checked_meta_run(metas)](crucible::MetaLog::Producer& end) noexcept {
                return crucible::MetaLog::try_append(end, run);
            });
    }

private:
    ::foundation::effects::host::ProducerClaim<Stranger> claim_;
    ::fixy::concurrent::SpscHeldSplit<crucible::MetaLog, crucible::VigilLogSides> log_;
};

}  // namespace neg_fixture

int main() { return 0; }
