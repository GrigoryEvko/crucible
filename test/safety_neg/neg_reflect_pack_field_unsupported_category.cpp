// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// pack_field covers exactly the categories IsReflectFieldSupported
// names: enum, integral, floating-point, pointer, array and class.  A
// member of any other category must reach the trailing static_assert.
//
// The array arm was absent until recently, so a struct with a C array
// member reached that same static_assert and was reported as outside
// IsReflectFieldSupported, which the concept says it is not.  This
// fixture holds the guard in place after the arm was added: a
// pointer-to-member is genuinely outside the concept, and is what the
// assert is for.

#include <crucible/Reflect.h>

struct Target {
    int field;
};

struct HasMemberPointer {
    int Target::* selector;
};

static_assert(!crucible::detail_reflect::IsReflectFieldSupported<int Target::*>,
              "a pointer-to-member is outside the concept; if this ever becomes true the fixture "
              "below stops testing the trailing static_assert.");

int main() {
    HasMemberPointer probe{&Target::field};
    auto packed = crucible::reflect_fmix_fold<0xDEADBEEFULL>(probe);
    (void)packed;
    return 0;
}
