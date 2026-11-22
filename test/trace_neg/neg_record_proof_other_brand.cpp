// Negative-compile test.  This file must fail to compile.
//
// A state of one producer of its own holds the claim of its own brand, and
// asks that claim for a proof in the role of the record path.  The proof names
// the claim of that brand, and the metadata log takes only the proof of the
// claim of the Vigils.  A second claim gives no proof of the log, so the
// thread of another brand cannot publish the head while the Vigil records.

#include <crucible/MetaLog.h>

namespace neg_fixture {

class Stranger {
public:
    crucible::MetaIndex append(crucible::MetaLog& log, ::fixy::View<const crucible::TensorMeta> metas) noexcept {
        auto const context = claim_.mint_producer_context();
        return log.try_append_proven(claim_.prove_sole_writer<crucible::RecordProducerRole>(context), metas);
    }

private:
    ::foundation::effects::host::ProducerClaim<Stranger> claim_;
};

}  // namespace neg_fixture

int main() { return 0; }
