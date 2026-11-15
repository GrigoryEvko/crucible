// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// An OpRecordFrame holds the slots of one op until the end of the scope of
// that op.  A frame that moves to a longer scope would close after a frame
// that opened after it, and the frames of one stack must close in the
// reverse order of their opens.  So a frame does not move.

#include <crucible/ValidatedOp.h>

#include <utility>

int main() {
    crucible::OpRecordStack stack{};
    crucible::OpRecordFrame frame{stack, 1};
    const crucible::OpRecordFrame moved{std::move(frame)};
    return moved.is_open() ? 0 : 1;
}
