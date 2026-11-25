// A stored grade is a claim about the value, and the write of one asks for a
// grade_key<Authority>, as the constructor does.  A write with no key meets no
// function.

#include <foundation/algebra/Graded.h>

namespace {

namespace fa = ::foundation::algebra;
using GOneByte = fa::detail::GOneByte;
using Value = fa::detail::OneByteValue;
using Authority = fa::detail::self_test_authority;

bool raise() {
    GOneByte held{Authority::key(), Value{}, false};
    held.set_grade(true);
    return held.grade();
}

}  // namespace

int main() { return raise() ? 0 : 1; }
