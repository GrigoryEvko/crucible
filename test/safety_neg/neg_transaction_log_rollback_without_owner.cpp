// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A transaction log belongs to one thread.  Each member takes that
// thread's owner proof, so a thread that holds no proof cannot roll the
// log back, and the rollback race between the foreground and the publish
// stage does not compile.  This call names no proof.
//
// Expected diagnostic: no rollback overload that takes no argument.

#include <crucible/Transaction.h>
#include <fixy/Ctx.h>
#include <foundation/effects/Effect.h>

class Owner {
public:
    static Owner claim() noexcept { return Owner{}; }
    Owner(const Owner&) = delete;
    ~Owner() {}

private:
    Owner() noexcept {}
};

int main() {
    crucible::TransactionLog<16, Owner> log{::fixy::TestRunnerCtx{::foundation::effects::testing::test()}};
    (void)log.rollback();
    return 0;
}
