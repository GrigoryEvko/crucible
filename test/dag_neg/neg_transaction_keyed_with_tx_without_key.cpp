// Negative compile test.  This file must fail to compile.
//
// A keyed TransactionLog lends a committed transaction to a body only with its
// LoanKey.  A draft holds the key, so a read with the key cannot run while a
// draft of the log lives.  The keyless door of a log of Unkeyed refuses a
// keyed log with its reason.

#include <crucible/Transaction.h>
#include <fixy/Core.h>
#include <fixy/CyclicBuffer.h>

#include <utility>

// A valid owner, so the read is refused for its missing key alone.
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
    auto minted = ::fixy::mint_cyclic_buffer<crucible::Transaction, 4>();
    auto log = crucible::build_transaction_log<Owner>(std::move(minted.at(::fixy::index<0>())));
    auto& key = minted.at(::fixy::index<1>());
    auto committed = log.active(key, owner);
    auto step = log.with_tx(owner, std::move(committed).expect("a test of the compile"),
                            [](crucible::Transaction const& tx) noexcept { return tx.step_id.get(); });
    static_cast<void>(step);
    return 0;
}
