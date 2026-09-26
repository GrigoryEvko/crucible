// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A transaction log holds a monotonic clock reader, so it is built only by
// a context whose row claims Bg, Init or Test.  This context holds the
// background capability, which could authorize the Bg effect, but its row
// claims only Alloc.  A capability is not a claim: CtxFitsTransactionLog
// reads the row, and it refuses the context.
//
// Companion: neg_transaction_log_foreground_ctx.cpp refuses the foreground
// context, whose capability permits nothing.

#include <crucible/Transaction.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>
#include <foundation/effects/Row.h>

class Owner {
    Owner() noexcept {}

public:
    Owner(const Owner&) = delete;
    ~Owner() {}
};

namespace fe = ::foundation::effects;

int main() {
    const fe::ExecCtx<fe::Bg, fe::Row<fe::Effect::Alloc>> alloc_only{fe::testing::bg()};
    crucible::TransactionLog<16, Owner> log{alloc_only};
    (void)log;
    return 0;
}
