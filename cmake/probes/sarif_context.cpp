// A capability probe of cmake/PatchedGccProbe.cmake.  Do not build it.
//
// The call of use() in probe() does not satisfy the constraint of use(), so
// the compiler refuses the file.  The text output of the error holds the
// constraint chain: "required for the satisfaction of 'has_size<T>'".  A
// compiler with the fix in utils/toolchain/gcc/patches/0003-*.patch also
// writes each such line to its SARIF log, as a related location with the
// property "gcc/context" of the value "constraint".  A compiler without the
// fix writes the line only to its text output.
template <typename T>
concept has_size = requires(T value) { value.size(); };

template <has_size T>
void use(T) {}

void probe() { use(0); }
