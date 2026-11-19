// The probe of utils/toolchain/gcc/patches/0007-*.patch.  With
// -fannotations-on-definition-only, the patched compiler refuses the
// annotation on the forward declaration of a class, and its SARIF log holds
// the error.  The stock compiler does not know the option, and without the
// option it takes the annotation as an annotation of the class.

struct mark final {};

struct[[= mark{}]] forward_declared;
struct forward_declared {};
