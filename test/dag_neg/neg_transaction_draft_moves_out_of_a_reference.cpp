// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A helper takes a draft by a reference and moves it out, as a hand-over to a
// holder does.  The caller would keep a second draft of one claim, and the two
// would commit the one transaction two times.  A draft does not move: it stays
// where begin_tx built it, and commit or abandon takes it by reference.

#include <crucible/Transaction.h>

#include <utility>

class Owner {
public:
    static Owner claim() noexcept { return Owner{}; }
    Owner(const Owner&) = delete;
    ~Owner() {}

private:
    Owner() noexcept {}
};

using Log = crucible::TransactionLog<16, Owner>;

Log::Draft hand_over(Log::Draft& draft) noexcept;
Log::Draft hand_over(Log::Draft& draft) noexcept { return std::move(draft); }

int main() {
    const Owner owner = Owner::claim();
    Log log{};
    auto draft = log.begin_tx(owner, 1);
    log.abandon(owner, hand_over(draft));
    return 0;
}
