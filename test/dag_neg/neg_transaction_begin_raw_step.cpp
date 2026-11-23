// Negative compile test.  This file must fail to compile.
//
// begin_tx takes the step of the transaction as a StepId, the step of the
// session log and of the Cipher log of a Vigil.  A raw integer has no
// conversion to a StepId, so a count of another kind, such as the place of
// the claim in the log, cannot stand for the step.

#include <crucible/Transaction.h>
#include <fixy/Core.h>

#include <cstdint>

// A valid owner, so the claim is refused for its raw step alone.
class Owner {
public:
    static Owner claim() noexcept { return Owner{}; }
    Owner(const Owner&) = delete;
    ~Owner() {}

private:
    Owner() noexcept {}
};

int main() {
    const Owner owner = Owner::claim();
    crucible::TransactionLog<16, Owner> log{};
    std::uint64_t const claim_place = 1;
    auto draft = log.begin_tx(owner, claim_place);
    log.abandon(owner, std::move(draft));
    return 0;
}
