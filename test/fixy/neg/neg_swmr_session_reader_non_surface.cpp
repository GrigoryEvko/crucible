// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// mint_swmr_reader asks for a channel of the SWMR shape.  An int is not
// a channel, so the mint refuses it.
//
// Expected diagnostic: SwmrSessionSurface is not satisfied.

#include <fixy/concurrent/SwmrSession.h>

int main() {
    namespace ses = ::fixy::concurrent::swmr_session;
    int not_a_channel = 0;
    auto reader = ses::mint_swmr_reader<int>(not_a_channel);
    (void)reader;
    return 0;
}
