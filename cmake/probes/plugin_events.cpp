// A capability probe of cmake/PatchedGccProbe.cmake.  Do not build it.
//
// utils/toolchain/gcc/patches/0004-*.patch gives a plugin six events of the
// C++ front end of GCC: PLUGIN_EXPLICIT_INSTANTIATION,
// PLUGIN_SIZEOF_OR_ALIGNOF, PLUGIN_NAME_USE, PLUGIN_USING_FORM,
// PLUGIN_MEMBER_ACCESS and PLUGIN_REFLECTION.  The rule proof-routes of the
// quarantine plugin of utils/tools/quarantine/ reads the first two.  The
// probe compiles with the plugin headers of the compiler, whose plugin.def
// gives each event.  A compiler with the patch accepts this file.  A compiler
// without the patch refuses each static_assert, because it has no constant
// for the six events.
#define DEFEVENT(NAME) inline constexpr char NAME##_text[] = #NAME;
#include <plugin.def>
#undef DEFEVENT

static_assert(sizeof(PLUGIN_EXPLICIT_INSTANTIATION_text) > 1);
static_assert(sizeof(PLUGIN_SIZEOF_OR_ALIGNOF_text) > 1);
static_assert(sizeof(PLUGIN_NAME_USE_text) > 1);
static_assert(sizeof(PLUGIN_USING_FORM_text) > 1);
static_assert(sizeof(PLUGIN_MEMBER_ACCESS_text) > 1);
static_assert(sizeof(PLUGIN_REFLECTION_text) > 1);

int main() { return 0; }
