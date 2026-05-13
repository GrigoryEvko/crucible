// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.

#include <crucible/ir001/Comm.h>

namespace ir = crucible::forge::ir001;

void consume(ir::DeclaredIr001Node<ir::AllReduceOp>) {}

int main() {
    ir::AllReduceOp raw{};
    consume(raw);
}
