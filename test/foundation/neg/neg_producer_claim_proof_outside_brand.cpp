// A state that is not the brand reaches the claim of the brand through a
// reference that the brand gives away, and asks it for the proof of the one
// writer.  The door of the proof is private, and the brand is its one friend,
// so only the brand asks, with the context that it minted in the same call.

#include <foundation/effects/Ctx.h>

namespace neg_fixture {

struct WriterRole {};

class Brand {
public:
    ::foundation::effects::host::ProducerClaim<Brand>& claim() noexcept { return claim_; }

private:
    ::foundation::effects::host::ProducerClaim<Brand> claim_;
};

struct Outsider {
    static void prove(Brand& brand) noexcept {
        ::foundation::effects::host::ProducerClaim<Brand>& claim = brand.claim();
        auto const context = claim.mint_producer_context();
        static_cast<void>(claim.prove_sole_writer<WriterRole>(context));
    }
};

}  // namespace neg_fixture

int main() {
    neg_fixture::Brand brand;
    neg_fixture::Outsider::prove(brand);
    return 0;
}
