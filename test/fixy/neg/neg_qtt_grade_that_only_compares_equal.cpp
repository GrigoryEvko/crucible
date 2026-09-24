// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A usage grade of Qtt is a QttGrade and nothing else.  The class below
// is not a grade, but it compares equal to the One grade, so a check on
// the value alone admits it as an exactly-once wrapper.  The type check
// of IsConsumeBound is what refuses it.  Naming the specialization is
// enough: the pointer below instantiates no class.
//
// Expected diagnostic: the template constraint of Qtt is not satisfied,
// and the note names the type check of IsConsumeBound.

#include <fixy/Qtt.h>

namespace {
struct LookalikeGrade {
    constexpr bool operator==(::foundation::algebra::lattices::QttGrade) const noexcept { return true; }
};
}  // namespace

int main() {
    [[maybe_unused]] ::fixy::Qtt<LookalikeGrade{}, int>* forged = nullptr;
    return 0;
}
