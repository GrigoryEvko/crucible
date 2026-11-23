// Negative compile test.  This file must fail to compile.
//
// A CommittedTx of a keyed TransactionLog holds the brand of its log.  Two
// mints give two logs of two brands, so a CommittedTx of one log reaches no
// door of the other: the slot and the claim of one log would name a
// different transaction in the second.

#include <crucible/Transaction.h>
#include <fixy/Core.h>
#include <fixy/CyclicBuffer.h>

#include <utility>

// A valid owner, so the activation is refused for its CommittedTx alone.
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
    auto first_minted = ::fixy::mint_cyclic_buffer<crucible::Transaction, 4>();
    auto second_minted = ::fixy::mint_cyclic_buffer<crucible::Transaction, 4>();
    auto first = crucible::build_transaction_log<Owner>(std::move(first_minted.at(::fixy::index<0>())));
    auto second = crucible::build_transaction_log<Owner>(std::move(second_minted.at(::fixy::index<0>())));
    auto& first_key = first_minted.at(::fixy::index<1>());
    auto& second_key = second_minted.at(::fixy::index<1>());
    auto draft = first.begin_tx(std::move(first_key), owner, ::fixy::session::StepId{1});
    first_key = first.abandon(owner, std::move(draft));
    auto committed = first.active(first_key, owner);
    static_cast<void>(second.activate(second_key, owner, std::move(committed).expect("the first log has none")));
    return 0;
}
