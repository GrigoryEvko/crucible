// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// An OpRecordFrame holds the slots of one op until the end of the scope of
// that op.  A copy would give the same slots back two times, and the stack
// then gives them to a nested op while the outer op still reads them.  So a
// frame does not copy.

#include <crucible/ValidatedOp.h>

int main() {
    crucible::OpRecordStack stack{};
    const crucible::OpRecordFrame frame{stack, 1};
    const crucible::OpRecordFrame copy{frame};
    return copy.is_open() ? 0 : 1;
}
