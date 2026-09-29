// Affine<Linear<T>> downgrades an exactly-once obligation to
// at-most-once, which makes a required consume optional.  A wrapper
// whose whole purpose is linearity must refuse the one composition that
// dissolves it.
//
// The recogniser is structural, so this fixture stands on a reflection
// query over the Qtt template rather than on a table entry somebody has
// to remember to write.
//
// The second required diagnostic is the compiler's rendering of the
// instantiation, `Qtt<...QttGrade::Zero, fixy::Qtt<`, which the source
// never spells: it writes Affine<Linear<int>>.

#include <fixy/Qtt.h>

using AffineOverLinear = fixy::Affine<fixy::Linear<int>>;

// sizeof completes the type, which is what runs the class body's
// assertions.  No constructor is named, so the only error is the one
// this fixture documents.
static_assert(sizeof(AffineOverLinear) > 0);

int main() { return 0; }
