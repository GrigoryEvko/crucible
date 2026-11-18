// A value at the erased grade.
//
// The grade Zero states that no run time reads the value.  A wrapper at
// that grade would hold a value that a body reads, so the grade of the
// wrapper would state a false fact.  Qtt holds a value only at the grades
// One and AtMostOne, so a Qtt at the grade Zero does not compile.

#include <fixy/Qtt.h>

#include <foundation/algebra/lattices/QttSemiring.h>

static_assert(sizeof(fixy::Qtt<foundation::algebra::lattices::QttGrade::Zero, int>) > 0);

int main() { return 0; }
