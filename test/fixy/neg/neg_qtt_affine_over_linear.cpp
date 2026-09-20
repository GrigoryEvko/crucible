// Affine<Linear<T>> downgrades an exactly-once obligation to
// at-most-once, which makes a required consume optional.  This is the
// rejection the port shipped without: it carried both gates of
// fixy/Qtt.h and neither table's arms, so for a release this file
// compiled and the wrapper whose whole purpose is linearity admitted
// the one composition that dissolves it.
//
// The recogniser is structural now, so this fixture stands on a
// reflection query over the Qtt template rather than on a table entry
// somebody has to remember to write.
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
