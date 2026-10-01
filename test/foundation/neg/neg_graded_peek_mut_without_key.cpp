// A write in place keeps the grade over new bytes.  Under a lattice that
// states nothing about what its grade claims, the grade is read as a
// claim about the bytes, so the keyless mutable reference does not exist.

#include <foundation/algebra/Graded.h>

namespace {

namespace fa = ::foundation::algebra;
using GOneByte = fa::detail::GOneByte;
using Value = fa::detail::OneByteValue;
using Authority = fa::detail::self_test_authority;

char rewrite() {
    GOneByte held{Authority::key(), Value{}, false};
    held.peek_mut().c = 'x';
    return held.peek().c;
}

}  // namespace

int main() { return rewrite() == 'x' ? 0 : 1; }
