// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// mint_pipeline with no stages at all.  A pipeline of one stage is
// admitted, with no adjacent pair to check; a pipeline of none has
// nothing to start and is refused by the chain's floor of one.

#include <fixy/Ctx.h>
#include <fixy/concurrent/Pipeline.h>

int main() {
    fixy::HotFgCtx ctx;

    auto bad = fixy::concurrent::mint_pipeline(ctx);
    (void)bad;
    return 0;
}
