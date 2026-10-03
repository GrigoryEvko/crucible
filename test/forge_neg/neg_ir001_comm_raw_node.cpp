// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.

#include <crucible/forge/Ir001/Comm.h>

namespace ir = crucible::forge::ir001;

namespace {

void consume(ir::DeclaredIr001Node<ir::AllReduceOp>) {}

}  // namespace

int main() {
    ir::AllReduceOp raw{};
    consume(raw);
}
