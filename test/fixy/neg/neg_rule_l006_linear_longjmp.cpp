// L006: linear x longjmp.  A longjmp past a linear value's scope skips
// its consumption, so the resource leaks with no diagnostic: no
// destructor runs, and nothing on the path back records that the value
// was never consumed.
//
// The pack names one atom.  Linear is the strict pole of the Usage axis,
// so a binding that says nothing about Usage IS linear, and the rule
// reads that pole rather than an atom — which is why this fixture's pack
// is the control-flow atom alone.  Naming atom::affine or atom::copy
// beside it admits the binding, and test/fixy/test_collision.cpp holds
// that pair.
//
// The rationale string is mandatory on the atom and carries no meaning
// to the gate; it exists so an audit of these sites reads why.

#include <fixy/Fn.h>

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::ctrl::longjmp_unsafe<"a setjmp island guards the parser">> refused{};
    return 0;
}
