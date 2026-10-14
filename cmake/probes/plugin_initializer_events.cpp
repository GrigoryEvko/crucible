// A capability probe of cmake/PatchedGccProbe.cmake.  Do not build it.
//
// utils/toolchain/gcc/patches/0005-*.patch gives a plugin two events of the
// C++ front end of GCC: PLUGIN_FOLDED_INITIALIZER and
// PLUGIN_EMPTY_CLASS_ASSIGNMENT.  The first gives an initializer before the
// front end replaces it with its folded value.  The second gives the target
// of a trivial assignment of an empty class, which the tree does not keep.
// The planned rule must-consume of the quarantine plugin of
// utils/tools/quarantine/ will read the two events.  The probe compiles with
// the plugin headers of the compiler, whose plugin.def gives each event.  A
// compiler without the patch refuses each static_assert, because it has no
// constant for them.
#define DEFEVENT(NAME) inline constexpr char NAME##_text[] = #NAME;
#include <plugin.def>
#undef DEFEVENT

static_assert(sizeof(PLUGIN_FOLDED_INITIALIZER_text) > 1);
static_assert(sizeof(PLUGIN_EMPTY_CLASS_ASSIGNMENT_text) > 1);

int main() { return 0; }
