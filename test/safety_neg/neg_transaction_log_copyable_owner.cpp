// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// An owner proof that can be copied can be handed to a second thread,
// and then two threads write the one log.  The log refuses such a type
// as its owner.
//
// Expected diagnostic: the OwnerProof constraint is not satisfied.

#include <crucible/Transaction.h>

struct CopyableOwner {};

int main() {
    // Naming the specialization is enough: the constraint is checked when
    // the template-id is formed, before any constructor is chosen.
    crucible::TransactionLog<16, CopyableOwner>* log = nullptr;
    (void)log;
    return 0;
}
