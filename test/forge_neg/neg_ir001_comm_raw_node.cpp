// Negative compile test. This file must fail to compile.

#include <crucible/forge/Ir001/Comm.h>

namespace ir = crucible::forge::ir001;

namespace {

void consume(ir::DeclaredIr001Node<ir::AllReduceOp>) {}

}  // namespace

int main() {
    ir::AllReduceOp raw{};
    consume(raw);
}
